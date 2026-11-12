/*
 * Smart Parking: automatic entrance gate (ESP32 + HC-SR04 + servo)
 * ----------------------------------------------------------------
 * Board: any ESP32 DevKit (Arduino IDE: Tools > Board > esp32 > "ESP32 Dev Module").
 * Needs only the ESP32 board package (Espressif, version 2.x or 3.x). No extra libraries.
 *
 * How it works
 *  - The HC-SR04 is read every 70 ms without blocking (the echo is timed by an interrupt).
 *  - A vehicle closer than DETECT_CM for a few readings in a row counts as detected.
 *  - AUTOMATIC mode: a detected vehicle opens the barrier (servo 0 -> 90 degrees, one degree at a time).
 *  - The barrier only closes by itself if a second "clearance" HC-SR04 after the barrier says the lane
 *    has been clear for CLEAR_HOLD_MS. With one sensor it stays open until someone presses CLOSE GATE.
 *  - CLOSE is refused while a vehicle is detected, and a closing barrier reopens if one appears.
 *  - EMERGENCY STOP freezes the barrier where it is until RESET is sent.
 *
 * Website link (Firebase Realtime Database, over HTTPS with certificate checking)
 *  - The ESP32 signs in to Firebase with its own account and writes its status to /gate/state.
 *  - The website writes commands to /gate/command; only the verified manager account may do that
 *    (enforced by the database rules in firebase/database.rules.json).
 *  - The network runs in its own task on core 0, so a slow connection never freezes the gate logic,
 *    and the gate keeps working on its own if Wi-Fi drops.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <sys/time.h>
#include "config.h"
#include "gate_types.h"
#include "root_ca.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy secrets.example.h to secrets.h (same folder) and fill in your Wi-Fi and Firebase details."
#endif

static const char *FW_VERSION = "1.0.0";

/* ================================================================ HC-SR04 (non-blocking) */

void IRAM_ATTR sonarIsr(void *arg) {
  Sonar *s = (Sonar *)arg;
  uint32_t now = micros();
  if (digitalRead(s->echo)) { s->riseUs = now; s->rose = true; }
  else if (s->rose) { s->fallUs = now; s->fell = true; }
}

void sonarBegin(Sonar &s, uint8_t trig, uint8_t echo) {
  s.trig = trig; s.echo = echo;
  pinMode(trig, OUTPUT);
  digitalWrite(trig, LOW);
  pinMode(echo, INPUT);
  attachInterruptArg(digitalPinToInterrupt(echo), sonarIsr, &s, CHANGE);
}

void sonarPing(Sonar &s) {
  s.rose = false;
  s.fell = false;
  digitalWrite(s.trig, HIGH);
  delayMicroseconds(10);        // 10 µs trigger pulse (the only wait in the sensor code)
  digitalWrite(s.trig, LOW);
  s.pingAtUs = micros();
  s.waiting = true;
}

// Median of the last three readings, with "nothing in range" sorted as far away.
static float median3(const float *v) {
  float a = isnan(v[0]) ? 1e6f : v[0], b = isnan(v[1]) ? 1e6f : v[1], c = isnan(v[2]) ? 1e6f : v[2];
  float m = max(min(a, b), min(max(a, b), c));
  return m >= 1e6f ? NAN : m;
}

static void sonarStore(Sonar &s, float cm) {
  s.last[s.idx] = cm;
  s.idx = (s.idx + 1) % 3;
  s.cm = median3(s.last);
}

// Returns true once the current ping has finished (with a reading, nothing in range, or no answer).
bool sonarPoll(Sonar &s) {
  if (!s.waiting) return false;
  if (s.fell) {
    s.waiting = false;
    s.silentPings = 0;
    s.fault = false;
    float cm = (s.fallUs - s.riseUs) / 58.0f;   // sound: about 58 µs per cm there and back
    sonarStore(s, (cm < 2.0f || cm > MAX_RANGE_CM) ? NAN : cm);
    return true;
  }
  if (micros() - s.pingAtUs > ECHO_TIMEOUT_US + 3000) {
    s.waiting = false;
    if (s.rose) {                                // echo started but never came back: nothing in range
      s.silentPings = 0;
      s.fault = false;
      sonarStore(s, NAN);
    } else if (++s.silentPings >= SENSOR_FAULT_PINGS) {
      s.fault = true;
      s.cm = NAN;
    }
    return true;
  }
  return false;
}

/* ================================================================ detection with hysteresis */

void detectorUpdate(Detector &d, float cm, float onCm, float offCm) {
  bool inside = !isnan(cm) && cm <= onCm;
  bool gone = isnan(cm) || cm > offCm;
  if (inside) {
    d.outCount = 0;
    if (!d.on && ++d.inCount >= DETECT_CONFIRM) d.on = true;
  } else {
    d.inCount = 0;
    if (d.on && gone) { if (++d.outCount >= RELEASE_CONFIRM) { d.on = false; d.outCount = 0; } }
    else d.outCount = 0;
  }
}

Sonar entrance;
Detector vehicle;
#if CLEAR_SENSOR_ENABLED
Sonar clearance;
Detector laneBusy;
#endif

/* ================================================================ servo (ESP32 LEDC PWM, 50 Hz) */

static const uint8_t SERVO_RES_BITS = 14;
#if ESP_ARDUINO_VERSION_MAJOR < 3
static const uint8_t SERVO_CHANNEL = 0;
#endif

void servoBegin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_SERVO, 50, SERVO_RES_BITS);
#else
  ledcSetup(SERVO_CHANNEL, 50, SERVO_RES_BITS);
  ledcAttachPin(PIN_SERVO, SERVO_CHANNEL);
#endif
}

void servoWrite(int deg) {
  deg = constrain(deg, 0, 180);
  uint32_t us = SERVO_MIN_US + (uint32_t)(SERVO_MAX_US - SERVO_MIN_US) * deg / 180;
  uint32_t duty = us * ((1UL << SERVO_RES_BITS) - 1) / 20000UL;   // 20 ms period
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_SERVO, duty);
#else
  ledcWrite(SERVO_CHANNEL, duty);
#endif
}

/* ================================================================ gate state machine */

static const char *STATE_NAMES[] = {"closed", "opening", "open", "closing", "stopped"};

struct Gate {
  GateState state = G_CLOSED;
  Mode mode = M_AUTO;
  int angle = SERVO_CLOSED_DEG;
  int target = SERVO_CLOSED_DEG;
  bool estop = false;
  uint32_t lastStepMs = 0;
  bool passSeen = false;        // clearance sensor saw the vehicle go through
  uint32_t clearSinceMs = 0;
  char note[96] = "Starting";
} gate;

static void setNote(const char *msg) {
  if (strncmp(gate.note, msg, sizeof(gate.note)) == 0) return;
  strncpy(gate.note, msg, sizeof(gate.note) - 1);
  gate.note[sizeof(gate.note) - 1] = 0;
  Serial.printf("[gate] %s\n", gate.note);
}

static bool laneBlocked() {
#if CLEAR_SENSOR_ENABLED
  return laneBusy.on || clearance.fault;
#else
  return false;
#endif
}

void startOpening(const char *why) {
  if (gate.estop) return;
  gate.target = SERVO_OPEN_DEG;
  gate.state = gate.angle == SERVO_OPEN_DEG ? G_OPEN : G_OPENING;
  gate.passSeen = false;
  gate.clearSinceMs = 0;
  setNote(why);
}

// Closing is refused unless every sensor says the way is clear.
bool startClosing(const char *why, const char **err) {
  if (gate.estop) { *err = "emergency stop is on"; return false; }
  if (entrance.fault) { *err = "entrance sensor is not answering"; return false; }
  if (vehicle.on) { *err = "a vehicle is at the gate"; return false; }
#if CLEAR_SENSOR_ENABLED
  if (clearance.fault) { *err = "clearance sensor is not answering"; return false; }
  if (laneBusy.on) { *err = "a vehicle is in the lane"; return false; }
#endif
  gate.target = SERVO_CLOSED_DEG;
  gate.state = gate.angle == SERVO_CLOSED_DEG ? G_CLOSED : G_CLOSING;
  setNote(why);
  return true;
}

void emergencyStop(const char *why) {
  gate.estop = true;
  gate.target = gate.angle;     // hold the barrier exactly where it is
  gate.state = G_STOPPED;
  setNote(why);
}

void resetStop() {
  gate.estop = false;
  gate.target = gate.angle;
  gate.state = gate.angle == SERVO_OPEN_DEG ? G_OPEN : gate.angle == SERVO_CLOSED_DEG ? G_CLOSED : G_STOPPED;
  setNote("Emergency stop released");
}

void gateUpdate(uint32_t nowMs) {
  if (!gate.estop) {
    // Safety first: a closing barrier goes back up if anything is detected.
    if (gate.state == G_CLOSING && (vehicle.on || entrance.fault || laneBlocked())) {
      startOpening("Reopened: vehicle detected while closing");
    }
    // Automatic mode: a vehicle at the entrance opens the gate.
    if (gate.mode == M_AUTO && vehicle.on && (gate.state == G_CLOSED || gate.state == G_STOPPED)) {
      startOpening("Opened automatically: vehicle detected");
    }
#if CLEAR_SENSOR_ENABLED
    // Automatic close: the clearance sensor saw the vehicle pass and the lane has stayed clear.
    if (gate.mode == M_AUTO && gate.state == G_OPEN) {
      if (laneBusy.on) { gate.passSeen = true; gate.clearSinceMs = 0; }
      else if (gate.passSeen && !vehicle.on && !clearance.fault && !entrance.fault) {
        if (!gate.clearSinceMs) gate.clearSinceMs = nowMs ? nowMs : 1;
        else if (nowMs - gate.clearSinceMs >= CLEAR_HOLD_MS) {
          const char *err;
          startClosing("Closed automatically: lane clear", &err);
        }
      }
    }
#else
    if (gate.state == G_OPEN) setNote("Open. Waiting for manual close (no clearance sensor)");
#endif
    // Smooth movement: one degree every SERVO_STEP_MS.
    if (gate.angle != gate.target && nowMs - gate.lastStepMs >= SERVO_STEP_MS) {
      gate.lastStepMs = nowMs;
      gate.angle += gate.target > gate.angle ? 1 : -1;
      servoWrite(gate.angle);
      if (gate.angle == gate.target) {
        gate.state = gate.target == SERVO_OPEN_DEG ? G_OPEN : G_CLOSED;
        setNote(gate.state == G_OPEN ? "Gate open" : "Gate closed");
      }
    }
  }
}

/* ================================================================ shared data between the two cores */

static portMUX_TYPE shareMux = portMUX_INITIALIZER_UNLOCKED;
static Snapshot shared;
static QueueHandle_t commandQueue;
static volatile uint8_t netLevel = 0;   // 0 no Wi-Fi, 1 Wi-Fi but no Firebase, 2 online

static void setLastCommand(const char *id, const char *result) {
  portENTER_CRITICAL(&shareMux);
  strncpy(shared.lastCmd, id, sizeof(shared.lastCmd) - 1);
  strncpy(shared.lastCmdResult, result, sizeof(shared.lastCmdResult) - 1);
  portEXIT_CRITICAL(&shareMux);
  Serial.printf("[cmd] %s -> %s\n", id, result);
}

void runCommand(const Command &c) {
  char result[72] = "ok";
  const char *err = nullptr;
  switch (c.action) {
    case A_OPEN:
      if (gate.estop) strcpy(result, "refused: emergency stop is on");
      else startOpening("Opened from the website");
      break;
    case A_CLOSE:
      if (!startClosing("Closing from the website", &err)) snprintf(result, sizeof(result), "refused: %s", err);
      break;
    case A_AUTO: gate.mode = M_AUTO; setNote("Automatic mode"); break;
    case A_MANUAL: gate.mode = M_MANUAL; setNote("Manual mode"); break;
    case A_ESTOP: emergencyStop("Emergency stop from the website"); break;
    case A_RESET:
      if (gate.estop) resetStop();
      else strcpy(result, "ok (was not stopped)");
      break;
  }
  setLastCommand(c.id, result);
}

void publishSnapshot() {
  portENTER_CRITICAL(&shareMux);
  shared.cm = entrance.cm;
  shared.sensorFault = entrance.fault;
  shared.vehicle = vehicle.on;
#if CLEAR_SENSOR_ENABLED
  shared.clearCm = clearance.cm; shared.clearFault = clearance.fault; shared.laneBusy = laneBusy.on;
#else
  shared.clearCm = NAN; shared.clearFault = false; shared.laneBusy = false;
#endif
  shared.state = gate.state; shared.mode = gate.mode; shared.angle = gate.angle; shared.estop = gate.estop;
  memcpy(shared.note, gate.note, sizeof(shared.note));
  portEXIT_CRITICAL(&shareMux);
}

/* ================================================================ network task (core 0) */

static WiFiClientSecure dbTls, authTls;
static HTTPClient dbHttp, authHttp;
static String idToken, refreshToken;
static uint32_t tokenRefreshAtMs = 0, authRetryAtMs = 0, authBackoffMs = 5000;
static String lastSeenCmd;
static bool firstPoll = true;
static uint8_t netFailures = 0;
static char bootId[9];

static uint64_t epochMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}
static bool timeSynced() { return time(nullptr) > 1700000000; }

// Reads one value from a flat JSON object (enough for Firebase's small replies).
static String jsonValue(const String &js, const char *key) {
  String k = String("\"") + key + "\"";
  int i = js.indexOf(k);
  if (i < 0) return "";
  i = js.indexOf(':', i + k.length());
  if (i < 0) return "";
  for (i++; i < (int)js.length() && isspace((unsigned char)js[i]); i++) {}
  if (i >= (int)js.length()) return "";
  if (js[i] == '"') {
    String out;
    for (int j = i + 1; j < (int)js.length(); j++) {
      char c = js[j];
      if (c == '\\' && j + 1 < (int)js.length()) { out += js[++j]; continue; }
      if (c == '"') return out;
      out += c;
    }
    return "";
  }
  int j = i;
  while (j < (int)js.length() && js[j] != ',' && js[j] != '}' && !isspace((unsigned char)js[j])) j++;
  return js.substring(i, j);
}

static String jsonEscape(const char *s) {
  String out;
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') out += '\\';
    out += *s;
  }
  return out;
}

static int httpCall(HTTPClient &http, WiFiClientSecure &tls, const char *method, const String &url,
                    const String &body, String &reply, const char *type = "application/json") {
  http.setReuse(true);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(tls, url)) return -1;
  if (body.length()) http.addHeader("Content-Type", type);
  int code = http.sendRequest(method, body);
  reply = code > 0 ? http.getString() : String();
  http.end();
  return code;
}

static bool storeToken(const String &reply, const char *idKey, const char *refreshKey, const char *expiresKey) {
  String tok = jsonValue(reply, idKey);
  if (!tok.length()) return false;
  idToken = tok;
  String r = jsonValue(reply, refreshKey);
  if (r.length()) refreshToken = r;
  long secs = jsonValue(reply, expiresKey).toInt();
  if (secs <= 0) secs = 3600;
  tokenRefreshAtMs = millis() + (uint32_t)max(60L, secs - 300) * 1000UL;   // refresh 5 minutes early
  authBackoffMs = 5000;
  return true;
}

static bool firebaseSignIn() {
  String reply;
  int code;
  if (refreshToken.length()) {
    code = httpCall(authHttp, authTls, "POST",
                    String("https://securetoken.googleapis.com/v1/token?key=") + FIREBASE_API_KEY,
                    "grant_type=refresh_token&refresh_token=" + refreshToken, reply,
                    "application/x-www-form-urlencoded");
    if (code == 200 && storeToken(reply, "id_token", "refresh_token", "expires_in")) {
      Serial.println("[firebase] token refreshed");
      authTls.stop();
      return true;
    }
    refreshToken = "";   // fall back to a full sign-in
  }
  String body = String("{\"email\":\"") + jsonEscape(DEVICE_EMAIL) + "\",\"password\":\"" +
                jsonEscape(DEVICE_PASSWORD) + "\",\"returnSecureToken\":true}";
  code = httpCall(authHttp, authTls, "POST",
                  String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FIREBASE_API_KEY,
                  body, reply);
  authTls.stop();   // free the TLS memory; sign-in happens about once an hour
  if (code == 200 && storeToken(reply, "idToken", "refreshToken", "expiresIn")) {
    Serial.printf("[firebase] signed in as %s (uid %s)\n", DEVICE_EMAIL, jsonValue(reply, "localId").c_str());
    return true;
  }
  Serial.printf("[firebase] sign-in failed (HTTP %d): %s\n", code, reply.c_str());
  return false;
}

static String dbUrl(const char *path) { return String(FIREBASE_DB_URL) + path + ".json?auth=" + idToken; }

static uint32_t pauseUntilMs = 0;   // back-off after errors, so a wrong setting never floods Firebase

static void noteNetResult(int code, const String &reply) {
  if (code >= 200 && code < 300) { netFailures = 0; netLevel = 2; return; }
  if (++netFailures >= 3) netLevel = 1;
  if ((code == 401 || code == 403) && reply.indexOf("oken") >= 0) {
    tokenRefreshAtMs = millis();   // the sign-in token ran out: get a new one
    return;
  }
  if (code == 401 || code == 403) {
    Serial.println("[firebase] permission denied: check the database rules and the ESP32 account UID in them");
    pauseUntilMs = millis() + 10000;
  } else if (code == 404) {
    Serial.println("[firebase] not found: check FIREBASE_DB_URL in secrets.h");
    pauseUntilMs = millis() + 10000;
  } else {
    pauseUntilMs = millis() + min((uint32_t)netFailures * 500, (uint32_t)5000);   // network trouble: retry a bit later
  }
}

static Action parseAction(const String &a, bool &ok) {
  ok = true;
  if (a == "open") return A_OPEN;
  if (a == "close") return A_CLOSE;
  if (a == "auto") return A_AUTO;
  if (a == "manual") return A_MANUAL;
  if (a == "estop") return A_ESTOP;
  if (a == "reset") return A_RESET;
  ok = false;
  return A_ESTOP;
}

static void pollCommand() {
  String reply;
  int code = httpCall(dbHttp, dbTls, "GET", dbUrl("/gate/command"), "", reply);
  noteNetResult(code, reply);
  if (code != 200 || reply == "null") { if (code == 200) firstPoll = false; return; }
  String id = jsonValue(reply, "id");
  if (!id.length() || id == lastSeenCmd) { firstPoll = false; return; }
  lastSeenCmd = id;
  if (firstPoll) { firstPoll = false; return; }   // a command from before this boot: never replay it
  uint64_t at = strtoull(jsonValue(reply, "at").c_str(), nullptr, 10);
  if (timeSynced() && at && epochMs() > at + CMD_MAX_AGE_MS) {
    setLastCommand(id.c_str(), "ignored: command was too old");
    return;
  }
  bool ok;
  Command c;
  strncpy(c.id, id.c_str(), sizeof(c.id) - 1);
  c.id[sizeof(c.id) - 1] = 0;
  c.action = parseAction(jsonValue(reply, "action"), ok);
  if (!ok) { setLastCommand(c.id, "ignored: unknown command"); return; }
  xQueueSend(commandQueue, &c, 0);
}

static String numOrNull(float v) { return isnan(v) ? String("null") : String(v, 1); }

static String stateJson(const Snapshot &s) {
  String j = "{";
  j += "\"fw\":\"" + String(FW_VERSION) + "\",\"bootId\":\"" + bootId + "\"";
  j += ",\"distanceCm\":" + numOrNull(s.cm);
  j += ",\"sensorOk\":" + String(s.sensorFault ? "false" : "true");
  j += ",\"vehicle\":" + String(s.vehicle ? "true" : "false");
  j += ",\"gate\":\"" + String(STATE_NAMES[s.state]) + "\"";
  j += ",\"angle\":" + String(s.angle);
  j += ",\"openAngle\":" + String(SERVO_OPEN_DEG) + ",\"closedAngle\":" + String(SERVO_CLOSED_DEG);
  j += ",\"stepMs\":" + String(SERVO_STEP_MS) + ",\"detectCm\":" + String(DETECT_CM, 1);
  j += ",\"mode\":\"" + String(s.mode == M_AUTO ? "auto" : "manual") + "\"";
  j += ",\"estop\":" + String(s.estop ? "true" : "false");
  j += ",\"clearSensor\":" + String(CLEAR_SENSOR_ENABLED ? "true" : "false");
  j += ",\"clearCm\":" + numOrNull(s.clearCm);
  j += ",\"clearOk\":" + String(s.clearFault ? "false" : "true");
  j += ",\"laneBusy\":" + String(s.laneBusy ? "true" : "false");
  j += ",\"note\":\"" + jsonEscape(s.note) + "\"";
  j += ",\"lastCmd\":\"" + jsonEscape(s.lastCmd) + "\",\"lastCmdResult\":\"" + jsonEscape(s.lastCmdResult) + "\"";
  return j;   // closed by sendState, which adds the live-only fields
}

static String lastSentState;
static uint32_t lastSentMs = 0;

static void maybeSendState() {
  Snapshot s;
  portENTER_CRITICAL(&shareMux);
  s = shared;
  portEXIT_CRITICAL(&shareMux);
  String body = stateJson(s);
  uint32_t now = millis();
  bool changed = body != lastSentState;
  if (!(changed && now - lastSentMs >= STATE_MIN_GAP_MS) && now - lastSentMs < HEARTBEAT_MS) return;
  String full = body + ",\"rssi\":" + String(WiFi.RSSI()) + ",\"uptimeS\":" + String(millis() / 1000) +
                ",\"updatedAt\":{\".sv\":\"timestamp\"}}";
  String reply;
  int code = httpCall(dbHttp, dbTls, "PUT", dbUrl("/gate/state"), full, reply);
  noteNetResult(code, reply);
  lastSentMs = now;
  if (code == 200) lastSentState = body;
  else Serial.printf("[firebase] status update failed (HTTP %d) %s\n", code, reply.c_str());
}

void netTask(void *) {
  dbTls.setCACert(ROOT_CA);
  authTls.setCACert(ROOT_CA);
  uint32_t lastWifiTry = 0, lastPoll = 0;
  bool timeStarted = false;
  for (;;) {
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      netLevel = 0;
      if (!lastWifiTry || now - lastWifiTry > 10000) {
        lastWifiTry = now;
        Serial.printf("[wifi] connecting to %s...\n", WIFI_SSID);
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!timeStarted) {
      timeStarted = true;
      Serial.printf("[wifi] connected, IP %s, signal %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      configTime(0, 0, "pool.ntp.org", "time.google.com");   // needed to check command age and certificates
    }
    if (!timeSynced()) { netLevel = 1; vTaskDelay(pdMS_TO_TICKS(250)); continue; }

    if (!idToken.length() || (int32_t)(now - tokenRefreshAtMs) >= 0) {
      if ((int32_t)(now - authRetryAtMs) < 0) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
      if (!firebaseSignIn()) {
        netLevel = 1;
        idToken = "";
        authRetryAtMs = now + authBackoffMs;
        authBackoffMs = min(authBackoffMs * 2, (uint32_t)60000);
        continue;
      }
    }
    if ((int32_t)(now - pauseUntilMs) < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    if (now - lastPoll >= CMD_POLL_MS) { lastPoll = now; pollCommand(); }
    if ((int32_t)(millis() - pauseUntilMs) >= 0) maybeSendState();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/* ================================================================ setup + main loop (core 1) */

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nSmart Parking entrance gate, firmware %s\n", FW_VERSION);

  pinMode(PIN_STATUS_LED, OUTPUT);
#if ESTOP_BUTTON_ENABLED
  pinMode(PIN_ESTOP_BUTTON, INPUT_PULLUP);
#endif
  servoBegin();
  servoWrite(gate.angle);       // start with the barrier down
  sonarBegin(entrance, PIN_TRIG, PIN_ECHO);
#if CLEAR_SENSOR_ENABLED
  sonarBegin(clearance, PIN_CLEAR_TRIG, PIN_CLEAR_ECHO);
#endif
  snprintf(bootId, sizeof(bootId), "%08lx", (unsigned long)esp_random());
  setNote("Gate closed");
  publishSnapshot();

  commandQueue = xQueueCreate(8, sizeof(Command));
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // faster replies to the website
  WiFi.setAutoReconnect(true);
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}

void loop() {
  static uint32_t lastPingMs = 0;
  static bool pingClear = false;
  uint32_t now = millis();

  // 1. Sensors: one ping at a time; with two sensors they take turns so they never hear each other.
  bool busy = entrance.waiting;
#if CLEAR_SENSOR_ENABLED
  busy = busy || clearance.waiting;
#endif
  if (!busy && now - lastPingMs >= PING_INTERVAL_MS) {
    lastPingMs = now;
#if CLEAR_SENSOR_ENABLED
    if (pingClear) sonarPing(clearance); else sonarPing(entrance);
    pingClear = !pingClear;
#else
    (void)pingClear;
    sonarPing(entrance);
#endif
  }
  if (sonarPoll(entrance)) detectorUpdate(vehicle, entrance.fault ? NAN : entrance.cm, DETECT_CM, DETECT_CM + RELEASE_HYSTERESIS_CM);
#if CLEAR_SENSOR_ENABLED
  if (sonarPoll(clearance)) detectorUpdate(laneBusy, clearance.fault ? NAN : clearance.cm, CLEAR_DETECT_CM, CLEAR_DETECT_CM + RELEASE_HYSTERESIS_CM);
#endif

  // 2. Local emergency stop button.
#if ESTOP_BUTTON_ENABLED
  static uint8_t pressed = 0;
  pressed = digitalRead(PIN_ESTOP_BUTTON) == LOW ? min(pressed + 1, 10) : 0;
  if (pressed == 5 && !gate.estop) emergencyStop("Emergency stop button pressed");
#endif

  // 3. Commands from the website.
  Command c;
  while (xQueueReceive(commandQueue, &c, 0) == pdTRUE) runCommand(c);

  // 4. Gate logic and servo movement.
  gateUpdate(now);
  publishSnapshot();

  // 5. Status LED: steady = online, slow blink = connecting, fast blink = emergency stop.
  bool led = gate.estop ? (now / 100) % 2 : netLevel == 2 ? true : (now / 500) % 2;
  digitalWrite(PIN_STATUS_LED, led);

  delay(1);
}
