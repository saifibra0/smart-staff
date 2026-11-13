/*
 * Smart Parking: automatic entrance gate (ESP32 + HC-SR04 + servo)
 * ----------------------------------------------------------------
 * Board: any ESP32 DevKit (Arduino IDE: Tools > Board > esp32 > "ESP32 Dev Module").
 * Needs only the ESP32 board package (Espressif, version 2.x or 3.x). No extra libraries.
 *
 * How it works
 *  - The HC-SR04 is read every 70 ms without blocking (the echo is timed by an interrupt).
 *  - A vehicle in the detection zone (DETECT_MIN_CM to DETECT_CM, 5 to 10 cm) for a few readings in a
 *    row counts as detected. Once detected it stays detected until it moves beyond the zone.
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
 *
 * Wi-Fi setup from a laptop or phone
 *  - With no working Wi-Fi, the ESP32 opens its own Wi-Fi "SmartGate-Setup". Join it and a setup page
 *    opens (or go to http://192.168.4.1): pick your Wi-Fi, type its password and the ESP32 account
 *    password, and save. Settings saved there win over secrets.h.
 *
 * On the same Wi-Fi as your laptop (local network)
 *  - http://smart-gate.local shows a view-only status page straight from the ESP32 (no internet needed).
 *  - After the first USB upload, new firmware can be uploaded over Wi-Fi (Arduino IDE network port
 *    "smart-gate", or esp32/upload-wifi.ps1). It needs an update password and only runs while the gate is
 *    closed and nothing is in front of the sensor.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "mbedtls/base64.h"
#include <SPI.h>
#include <MFRC522.h>
#include <sys/time.h>
#include "config.h"
#include "gate_types.h"
#include "root_ca.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy secrets.example.h to secrets.h (same folder) and fill in your Wi-Fi and Firebase details."
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""   // an older secrets.h without it: Wi-Fi updates stay off
#endif

static const char *FW_VERSION = "1.7.1";

/* ================================================================ settings: setup page first, then secrets.h */

struct Settings { String ssid, wifiPass, devicePass, otaPass; };
static Settings cfg;
static volatile bool setupActive = false;     // the ESP32's own setup Wi-Fi is open
static volatile uint8_t setupRequest = 0;     // 1 = open the setup Wi-Fi, 2 = close it (done by the local task)
static const char *setupWhy = "";

static bool isPlaceholder(const char *v) {
  return !*v || !strcmp(v, "your-wifi-name") || !strcmp(v, "your-wifi-password") || !strcmp(v, "the-password-you-set") ||
         !strcmp(v, "choose-a-long-random-password") || !strcmp(v, "choose-an-update-password");
}

// Values saved from the setup page win; otherwise the ones in secrets.h, unless they are still placeholders.
static void loadSettings() {
  Preferences p;
  if (p.begin("gate", true)) {
    cfg.ssid = p.getString("ssid", "");
    cfg.wifiPass = p.getString("wpass", "");
    cfg.devicePass = p.getString("dpass", "");
    cfg.otaPass = p.getString("opass", "");
    p.end();
  }
  if (!cfg.ssid.length() && !isPlaceholder(WIFI_SSID)) {
    cfg.ssid = WIFI_SSID;
    cfg.wifiPass = isPlaceholder(WIFI_PASSWORD) ? "" : WIFI_PASSWORD;
  }
  if (!cfg.devicePass.length() && !isPlaceholder(DEVICE_PASSWORD)) cfg.devicePass = DEVICE_PASSWORD;
  if (!cfg.otaPass.length() && !isPlaceholder(OTA_PASSWORD) && strlen(OTA_PASSWORD) >= 8) cfg.otaPass = OTA_PASSWORD;
}

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
// blank = the servo is moving: a missing echo then is most likely the servo pulling the 5 V down,
// so it does not count towards a sensor fault (real readings still count).
static bool echoLostWhileMoving = false;
bool sonarPoll(Sonar &s, bool blank) {
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
    } else if (blank) {
      echoLostWhileMoving = true;
    } else if (++s.silentPings >= SENSOR_FAULT_PINGS) {
      s.fault = true;
      s.cm = NAN;
    }
    return true;
  }
  return false;
}

/* ================================================================ detection with hysteresis */

// Detected after a few readings in [minCm, onCm]; released only after a few readings beyond offCm (or nothing
// in range). Readings closer than minCm do not trigger, but they do keep an existing detection.
void detectorUpdate(Detector &d, float cm, float minCm, float onCm, float offCm) {
  bool inside = !isnan(cm) && cm >= minCm && cm <= onCm;
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
  bool ok = ledcAttach(PIN_SERVO, 50, SERVO_RES_BITS);
#else
  bool ok = ledcSetup(SERVO_CHANNEL, 50, SERVO_RES_BITS) > 0;
  ledcAttachPin(PIN_SERVO, SERVO_CHANNEL);
#endif
  Serial.printf("[servo] signal on GPIO %d: %s\n", PIN_SERVO, ok ? "running (50 Hz)" : "FAILED to start");
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

static uint32_t lastServoStepMs = 0;   // when the servo last moved (for sensor blanking)

// Updating the servo signal blocks for about 40 ms on this ESP32 core (it waits for two 20 ms servo
// periods). Doing that in the main loop held up the sensor's echo timing, so a separate task on the
// other core applies the newest wanted angle, and the gate logic just sets servoWantDeg.
static volatile int servoWantDeg = SERVO_CLOSED_DEG;
void servoTask(void *) {
  int applied = -1000;
  for (;;) {
    int want = servoWantDeg;
    if (want != applied) { servoWrite(want); applied = want; }
    else vTaskDelay(pdMS_TO_TICKS(5));
  }
}

struct Gate {
  GateState state = G_CLOSED;
  Mode mode = M_AUTO;
  int angle = SERVO_CLOSED_DEG;
  int target = SERVO_CLOSED_DEG;
  bool estop = false;
  uint32_t lastStepMs = 0;
  uint32_t moveStartMs = 0;     // when the current movement began
  int moveFromAngle = SERVO_CLOSED_DEG;   // where it began
  uint32_t slowestLoopMs = 0;   // longest gap between gate updates during the movement
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

// Anything within the detection distance, even closer than the zone, blocks closing.
static bool somethingClose() { return !isnan(entrance.cm) && entrance.cm <= DETECT_CM; }

static bool laneBlocked() {
#if CLEAR_SENSOR_ENABLED
  return laneBusy.on || clearance.fault;
#else
  return false;
#endif
}

void startOpening(const char *why) {
  if (gate.estop) return;
  if (gate.target != SERVO_OPEN_DEG) { gate.moveStartMs = millis(); gate.moveFromAngle = gate.angle; gate.slowestLoopMs = 0; }
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
  if (somethingClose()) { *err = "something is right in front of the sensor"; return false; }
#if CLEAR_SENSOR_ENABLED
  if (clearance.fault) { *err = "clearance sensor is not answering"; return false; }
  if (laneBusy.on) { *err = "a vehicle is in the lane"; return false; }
#endif
  if (gate.target != SERVO_CLOSED_DEG) { gate.moveStartMs = millis(); gate.moveFromAngle = gate.angle; gate.slowestLoopMs = 0; }
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
  static uint32_t lastUpdateMs = 0;
  if (gate.angle != gate.target && lastUpdateMs && nowMs - lastUpdateMs > gate.slowestLoopMs) gate.slowestLoopMs = nowMs - lastUpdateMs;
  lastUpdateMs = nowMs;
  if (!gate.estop) {
    // Safety first: a closing barrier goes back up if anything is detected.
    if (gate.state == G_CLOSING && (vehicle.on || somethingClose() || entrance.fault || laneBlocked())) {
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
    // One sensor: close a few seconds after the vehicle has left the zone (it cannot see under the barrier).
    if (gate.mode == M_AUTO && gate.state == G_OPEN && AUTO_CLOSE_AFTER_MS > 0) {
      if (vehicle.on || somethingClose() || entrance.fault) {
        gate.clearSinceMs = 0;
        setNote("Open. Vehicle at the gate");
      } else if (!gate.clearSinceMs) {
        gate.clearSinceMs = nowMs ? nowMs : 1;
        setNote("Open. No vehicle: closing in a few seconds");
      } else if (nowMs - gate.clearSinceMs >= AUTO_CLOSE_AFTER_MS) {
        const char *err;
        startClosing("Closed automatically: no vehicle", &err);
      }
    } else if (gate.state == G_OPEN) {
      setNote(gate.mode == M_MANUAL ? "Open. Manual mode: press CLOSE GATE" : "Open. Waiting for manual close (no clearance sensor)");
    }
#endif
    // Smooth movement at 1 degree per SERVO_STEP_MS, timed by the clock: if an update comes late the
    // barrier catches up, so 0 -> 90 degrees always takes about 90 x SERVO_STEP_MS.
    if (gate.angle != gate.target) {
      int span = abs(gate.target - gate.moveFromAngle);
      int done = min((int)((nowMs - gate.moveStartMs) / SERVO_STEP_MS), span);
      int next = gate.moveFromAngle + (gate.target > gate.moveFromAngle ? done : -done);
      if (next != gate.angle) {
      gate.lastStepMs = nowMs;
      gate.angle = next;
      servoWantDeg = gate.angle;
      lastServoStepMs = nowMs;
      if (gate.angle == gate.target) {
        Serial.printf("[servo] reached %d deg (%lu us pulse) in %lu ms; slowest update gap %lu ms\n", gate.angle,
                      (unsigned long)(SERVO_MIN_US + (uint32_t)(SERVO_MAX_US - SERVO_MIN_US) * gate.angle / 180),
                      (unsigned long)(nowMs - gate.moveStartMs), (unsigned long)gate.slowestLoopMs);
        gate.state = gate.target == SERVO_OPEN_DEG ? G_OPEN : G_CLOSED;
        setNote(gate.state == G_OPEN ? "Gate open" : "Gate closed");
      }
      }
    }
  }
}

/* ================================================================ shared data between the two cores */

static portMUX_TYPE shareMux = portMUX_INITIALIZER_UNLOCKED;
static Snapshot shared;
static QueueHandle_t commandQueue;
static volatile uint8_t netLevel = 0;   // 0 no Wi-Fi, 1 Wi-Fi but no Firebase, 2 online
static volatile bool otaAllowed = false; // gate closed and idle: safe to accept a Wi-Fi update
static volatile bool otaActive = false;  // a Wi-Fi update is being written

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

/* ================================================================ RFID cards (RC522 on SPI) */

// Allowed cards come from Firebase /gate/cards (managed on the website) and are cached in flash,
// so they keep working when the Wi-Fi is down.
static CardEntry cardList[MAX_CARDS];
static uint8_t cardListCount = 0;
static portMUX_TYPE cardMux = portMUX_INITIALIZER_UNLOCKED;
static CardEvent cardEvent;                 // the last tap, waiting to be reported to the website
static volatile uint32_t cardEventSeq = 0;
static volatile bool cardsSyncNow = true;   // read the card list again as soon as possible
static bool rfidOk = false;
#if RFID_ENABLED
static MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
#endif

static void uidHex(const byte *b, byte n, char *out) {
  static const char *hex = "0123456789ABCDEF";
  byte k = 0;
  for (byte i = 0; i < n && k < 20; i++) { out[k++] = hex[b[i] >> 4]; out[k++] = hex[b[i] & 15]; }
  out[k] = 0;
}

static bool cardAllowed(const char *uid, char *nameOut) {
  bool found = false;
  portENTER_CRITICAL(&cardMux);
  for (uint8_t i = 0; i < cardListCount; i++) {
    if (!strcmp(cardList[i].uid, uid)) { strcpy(nameOut, cardList[i].name); found = true; break; }
  }
  portEXIT_CRITICAL(&cardMux);
  return found;
}

static void loadCardCache() {
  Preferences p;
  if (!p.begin("gate", true)) return;
  String s = p.getString("cards", "");
  p.end();
  uint8_t n = 0;
  int from = 0;
  while (from < (int)s.length() && n < MAX_CARDS) {
    int nl = s.indexOf('\n', from);
    if (nl < 0) nl = s.length();
    String line = s.substring(from, nl);
    int eq = line.indexOf('=');
    if (eq > 0) {
      CardEntry &c = cardList[n++];
      memset(&c, 0, sizeof(c));
      strncpy(c.uid, line.substring(0, eq).c_str(), sizeof(c.uid) - 1);
      strncpy(c.name, line.substring(eq + 1).c_str(), sizeof(c.name) - 1);
    }
    from = nl + 1;
  }
  cardListCount = n;
  if (n) Serial.printf("[card] %u allowed card(s) from the last sync\n", (unsigned)n);
}

static void saveCardCache() {
  static CardEntry copy[MAX_CARDS];
  uint8_t n;
  portENTER_CRITICAL(&cardMux);
  n = cardListCount;
  memcpy(copy, cardList, sizeof(CardEntry) * n);
  portEXIT_CRITICAL(&cardMux);
  String s;
  for (uint8_t i = 0; i < n; i++) { s += copy[i].uid; s += '='; s += copy[i].name; s += '\n'; }
  Preferences p;
  if (p.begin("gate", false)) { p.putString("cards", s); p.end(); }
}

// A card was tapped: open for an allowed card, then report the tap to the website.
static void onCard(const char *uid) {
  char name[33] = "";
  const char *result;
  if (!cardAllowed(uid, name)) result = "denied";
  else if (gate.estop) result = "estop";
  else {
    result = "granted";
    char why[96];
    snprintf(why, sizeof(why), "Opened by card: %s", name[0] ? name : uid);
    startOpening(why);
  }
  Serial.printf("[card] %s %s -> %s\n", uid, name[0] ? name : "(not registered)", result);
  portENTER_CRITICAL(&shareMux);
  strncpy(cardEvent.uid, uid, sizeof(cardEvent.uid) - 1);
  strncpy(cardEvent.name, name, sizeof(cardEvent.name) - 1);
  strncpy(cardEvent.result, result, sizeof(cardEvent.result) - 1);
  cardEventSeq = cardEventSeq + 1;
  cardEvent.seq = cardEventSeq;
  portEXIT_CRITICAL(&shareMux);
}

static void rfidBegin() {
#if RFID_ENABLED
  SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();
  delay(10);
  rfid.PCD_SetAntennaGain(RFID_GAIN);
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  rfidOk = v != 0x00 && v != 0xFF;
  Serial.printf("[card] RC522 reader %s (chip version 0x%02X)\n", rfidOk ? "found" : "NOT FOUND: check its wiring", v);
#endif
}

static void rfidPoll(uint32_t now) {
#if RFID_ENABLED
  static uint32_t lastPollMs = 0, lastTapMs = 0, lastInitMs = 0;
  static char lastUid[21] = "";
  if (now - lastPollMs < RFID_POLL_MS) return;
  lastPollMs = now;
  // RC522 clones can freeze: restart the chip every 10 s while idle (this also finds a reader connected later).
  if (now - lastInitMs > 10000 && now - lastTapMs > 3000) {
    lastInitMs = now;
    rfid.PCD_Init();
    rfid.PCD_SetAntennaGain(RFID_GAIN);
    byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
    bool ok = v != 0x00 && v != 0xFF;
    if (ok != rfidOk) { rfidOk = ok; Serial.printf("[card] RC522 reader %s\n", ok ? "found" : "lost"); }
  }
  if (!rfidOk) return;
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;
  char uid[21];
  uidHex(rfid.uid.uidByte, rfid.uid.size, uid);
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  bool again = !strcmp(uid, lastUid) && now - lastTapMs < 2000;   // a card held still is read again and again
  lastTapMs = now;
  if (again) return;
  strncpy(lastUid, uid, sizeof(lastUid) - 1);
  onCard(uid);
#else
  (void)now;
#endif
}

/* ================================================================ network task (core 0) */

static WiFiClientSecure dbTls, authTls;
static HTTPClient dbHttp, authHttp;
static String idToken, refreshToken;
static uint32_t tokenRefreshAtMs = 0, authRetryAtMs = 0, authBackoffMs = 5000;
static bool authLocked = false;                 // Firebase said TOO_MANY_ATTEMPTS_TRY_LATER
static const uint32_t AUTH_LOCKED_WAIT_MS = 15UL * 60UL * 1000UL;
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
  // 204 No Content has no body; asking for one would wait until Firebase closes the connection.
  reply = (code > 0 && code != 204) ? http.getString() : String();
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
                jsonEscape(cfg.devicePass.c_str()) + "\",\"returnSecureToken\":true}";
  code = httpCall(authHttp, authTls, "POST",
                  String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FIREBASE_API_KEY,
                  body, reply);
  authTls.stop();   // free the TLS memory; sign-in happens about once an hour
  if (code == 200 && storeToken(reply, "idToken", "refreshToken", "expiresIn")) {
    Serial.printf("[firebase] signed in as %s (uid %s)\n", DEVICE_EMAIL, jsonValue(reply, "localId").c_str());
    return true;
  }
  Serial.printf("[firebase] sign-in failed (HTTP %d): %s\n", code, reply.c_str());
  // After many wrong passwords Firebase blocks sign-in for a while. Every try during the block can
  // keep it going, so wait 15 minutes; the password itself may be right, so don't open the setup Wi-Fi.
  authLocked = reply.indexOf("TOO_MANY_ATTEMPTS_TRY_LATER") >= 0;
  if (authLocked) {
    Serial.println("[firebase] Firebase is blocking sign-in for a while (too many wrong passwords before); next try in 15 minutes");
  } else if (code == 400 && !setupActive) {
    setupWhy = "Firebase rejected the ESP32 account password";
    setupRequest = 1;
  }
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

// Handles /gate/command as JSON, from the live stream or the backup poll.
static void handleCommandJson(const String &reply) {
  String id = jsonValue(reply, "id");
  if (!id.length() || id == lastSeenCmd) { firstPoll = false; return; }
  lastSeenCmd = id;
  if (firstPoll) { firstPoll = false; return; }   // a command from before this boot: never replay it
  uint64_t at = strtoull(jsonValue(reply, "at").c_str(), nullptr, 10);
  if (timeSynced() && at && epochMs() >= at)
    Serial.printf("[cmd] %s reached the ESP32 %lu ms after the website sent it\n", id.c_str(), (unsigned long)(epochMs() - at));
  if (timeSynced() && at && epochMs() > at + CMD_MAX_AGE_MS) {
    setLastCommand(id.c_str(), "ignored: command was too old");
    return;
  }
  if (jsonValue(reply, "action") == "sync") {   // the website changed the allowed cards
    cardsSyncNow = true;
    setLastCommand(id.c_str(), "ok");
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

// Backup: ask for the command (used only while the live stream is down).
static void pollCommand() {
  String reply;
  int code = httpCall(dbHttp, dbTls, "GET", dbUrl("/gate/command"), "", reply);
  noteNetResult(code, reply);
  if (code != 200) return;
  if (reply == "null") { firstPoll = false; return; }
  handleCommandJson(reply);
}

/* ---------------- live command stream: Firebase pushes /gate/command the moment the website writes it */

static WiFiClientSecure sseTls;
static bool sseUp = false, sseChunked = false;
static long sseChunkLeft = 0;
static String sseToken;   // the sign-in token the open stream uses
static String sseBuf, sseEvent, sseData;
static uint32_t sseLastByteMs = 0, sseRetryAtMs = 0, sseBackoffMs = 2000;

static String sseReadLine(uint32_t timeoutMs) {
  String line;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (sseTls.available()) {
      char c = (char)sseTls.read();
      if (c == '\n') { line.trim(); return line; }
      if (line.length() < 1200) line += c;
    }
    if (!sseTls.connected()) break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  line.trim();
  return line;
}

static void sseClose(const char *why) {
  if (sseUp) Serial.printf("[firebase] command stream closed (%s)\n", why);
  sseTls.stop();
  sseUp = false;
  sseRetryAtMs = millis() + sseBackoffMs;
  sseBackoffMs = min(sseBackoffMs * 2, (uint32_t)30000);
}

// Opens GET /gate/command.json with Accept: text/event-stream, following Firebase's redirects.
static bool sseOpen() {
  String url = dbUrl("/gate/command");
  for (int hop = 0; hop < 3; hop++) {
    if (!url.startsWith("https://")) return false;
    int slash = url.indexOf('/', 8);
    String host = url.substring(8, slash < 0 ? url.length() : slash);
    String path = slash < 0 ? String("/") : url.substring(slash);
    sseTls.stop();
    sseTls.setCACert(ROOT_CA);
    if (!sseTls.connect(host.c_str(), 443, HTTP_TIMEOUT_MS)) { Serial.println("[firebase] command stream: could not connect"); return false; }
    sseTls.print(String("GET ") + path + " HTTP/1.1\r\nHost: " + host +
                 "\r\nAccept: text/event-stream\r\nConnection: keep-alive\r\n\r\n");
    String status = sseReadLine(HTTP_TIMEOUT_MS);
    int code = status.length() >= 12 ? status.substring(9, 12).toInt() : 0;
    String location;
    sseChunked = false;
    for (;;) {
      String h = sseReadLine(HTTP_TIMEOUT_MS);
      if (!h.length()) break;
      String lower = h;
      lower.toLowerCase();
      if (lower.startsWith("location:")) { location = h.substring(9); location.trim(); }
      if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") > 0) sseChunked = true;
    }
    if ((code == 301 || code == 302 || code == 307 || code == 308) && location.length()) { url = location; continue; }
    if (code == 200) {
      sseBuf = ""; sseEvent = ""; sseData = ""; sseChunkLeft = 0;
      sseLastByteMs = millis();
      sseBackoffMs = 2000;
      sseToken = idToken;
      Serial.println("[firebase] command stream open: website commands arrive instantly");
      return true;
    }
    if (code == 401) tokenRefreshAtMs = millis();   // the sign-in token ran out
    Serial.printf("[firebase] command stream refused (HTTP %d)\n", code);
    sseTls.stop();
    return false;
  }
  return false;
}

static void sseDispatch() {
  if (sseEvent == "put" || sseEvent == "patch") {
    if (jsonValue(sseData, "path") == "/" && sseData.indexOf("\"data\":null") < 0) handleCommandJson(sseData);
    else if (sseData.indexOf("\"data\":null") >= 0) firstPoll = false;
  } else if (sseEvent == "auth_revoked") {
    tokenRefreshAtMs = millis();   // the token expired: sign in again, then reopen
    sseClose("sign-in token expired");
  } else if (sseEvent == "cancel") {
    sseClose("access cancelled");
  }
  sseEvent = "";
  sseData = "";
}

// Reads whatever has arrived on the stream without waiting.
static void ssePump() {
  if (!sseTls.connected()) { sseClose("connection lost"); return; }
  while (sseTls.available()) {
    if (sseChunked && sseChunkLeft == 0) {
      String size = sseReadLine(1000);
      if (!size.length()) continue;                          // the CRLF after a chunk
      sseChunkLeft = strtol(size.c_str(), nullptr, 16);
      if (sseChunkLeft == 0) { sseClose("server ended the stream"); return; }
      continue;
    }
    char c = (char)sseTls.read();
    if (sseChunked) sseChunkLeft--;
    sseLastByteMs = millis();
    if (c == '\r') continue;
    if (c != '\n') { if (sseBuf.length() < 1500) sseBuf += c; continue; }
    if (!sseBuf.length()) { if (sseEvent.length()) sseDispatch(); }
    else if (sseBuf.startsWith("event:")) { sseEvent = sseBuf.substring(6); sseEvent.trim(); }
    else if (sseBuf.startsWith("data:")) { sseData = sseBuf.substring(5); sseData.trim(); }
    sseBuf = "";
    if (!sseUp) return;
  }
  // Firebase sends a keep-alive every 30 s; silence for longer means the connection died quietly.
  if (millis() - sseLastByteMs > 45000) sseClose("no keep-alive");
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
  j += ",\"stepMs\":" + String(SERVO_STEP_MS) + ",\"detectCm\":" + String(DETECT_CM, 1) + ",\"detectMinCm\":" + String(DETECT_MIN_CM, 1) + ",\"autoCloseMs\":" + String(CLEAR_SENSOR_ENABLED ? 0 : AUTO_CLOSE_AFTER_MS);
  j += ",\"mode\":\"" + String(s.mode == M_AUTO ? "auto" : "manual") + "\"";
  j += ",\"estop\":" + String(s.estop ? "true" : "false");
  j += ",\"clearSensor\":" + String(CLEAR_SENSOR_ENABLED ? "true" : "false");
  j += ",\"clearCm\":" + numOrNull(s.clearCm);
  j += ",\"clearOk\":" + String(s.clearFault ? "false" : "true");
  j += ",\"laneBusy\":" + String(s.laneBusy ? "true" : "false");
  j += ",\"note\":\"" + jsonEscape(s.note) + "\"";
  j += ",\"lastCmd\":\"" + jsonEscape(s.lastCmd) + "\",\"lastCmdResult\":\"" + jsonEscape(s.lastCmdResult) + "\"";
  j += ",\"rfid\":" + String(RFID_ENABLED ? (rfidOk ? "true" : "false") : "null") + ",\"cards\":" + String(cardListCount);
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
  int code = httpCall(dbHttp, dbTls, "PUT", dbUrl("/gate/state") + "&print=silent", full, reply);
  noteNetResult(code, reply);
  lastSentMs = now;
  if (code >= 200 && code < 300) lastSentState = body;
  else Serial.printf("[firebase] status update failed (HTTP %d) %s\n", code, reply.c_str());
}


// Reads {"058AC7C3":{"name":"Saif","at":...},...} into out[]; returns how many cards.
static uint8_t parseCards(const String &js, CardEntry *out, uint8_t max) {
  uint8_t n = 0;
  int len = js.length(), depth = 0;
  for (int i = 0; i < len && n < max; i++) {
    char c = js[i];
    if (c == '{') { depth++; continue; }
    if (c == '}') { depth--; continue; }
    if (c != '"') continue;
    int end = i + 1;
    while (end < len && js[end] != '"') { if (js[end] == '\\') end++; end++; }
    if (end >= len) break;
    if (depth == 1) {
      int k = end + 1;
      while (k < len && isspace((unsigned char)js[k])) k++;
      if (k < len && js[k] == ':') {
        k++;
        while (k < len && isspace((unsigned char)js[k])) k++;
        if (k < len && js[k] == '{') {
          int d = 0, j = k;
          for (; j < len; j++) {                       // the matching '}', skipping strings
            if (js[j] == '"') { j++; while (j < len && js[j] != '"') { if (js[j] == '\\') j++; j++; } continue; }
            if (js[j] == '{') d++;
            else if (js[j] == '}' && --d == 0) break;
          }
          String key = js.substring(i + 1, end);
          String name = jsonValue(js.substring(k, j + 1), "name");
          if (key.length() && key.length() < sizeof(out[n].uid)) {
            memset(&out[n], 0, sizeof(CardEntry));
            strncpy(out[n].uid, key.c_str(), sizeof(out[n].uid) - 1);
            strncpy(out[n].name, name.c_str(), sizeof(out[n].name) - 1);
            n++;
          }
          i = j;
          continue;
        }
      }
    }
    i = end;
  }
  return n;
}

// Reads the allowed cards from Firebase; keeps the cached list if that fails.
static void syncCards() {
  String reply;
  int code = httpCall(dbHttp, dbTls, "GET", dbUrl("/gate/cards"), "", reply);
  if (code != 200) {
    static int lastBad = 0;
    if (code != lastBad) Serial.printf("[card] could not read the card list (HTTP %d); using the saved list\n", code);
    lastBad = code;
    return;
  }
  static CardEntry fresh[MAX_CARDS];
  memset(fresh, 0, sizeof(fresh));
  uint8_t n = reply == "null" ? 0 : parseCards(reply, fresh, MAX_CARDS);
  bool changed;
  portENTER_CRITICAL(&cardMux);
  changed = n != cardListCount || memcmp(fresh, cardList, sizeof(CardEntry) * n) != 0;
  if (changed) { memcpy(cardList, fresh, sizeof(CardEntry) * n); cardListCount = n; }
  portEXIT_CRITICAL(&cardMux);
  if (changed) {
    Serial.printf("[card] %u allowed card(s) from the website\n", (unsigned)n);
    saveCardCache();
  }
}

// Reports the last tap to /gate/lastCard, once per tap.
static uint32_t sentCardSeq = 0;
static void maybeSendCard() {
  if (cardEventSeq == sentCardSeq) return;
  CardEvent e;
  portENTER_CRITICAL(&shareMux);
  e = cardEvent;
  portEXIT_CRITICAL(&shareMux);
  String body = String("{\"uid\":\"") + e.uid + "\",\"name\":\"" + jsonEscape(e.name) + "\",\"result\":\"" + e.result +
                "\",\"seq\":" + String(e.seq) + ",\"boot\":\"" + bootId + "\",\"at\":{\".sv\":\"timestamp\"}}";
  String reply;
  int code = httpCall(dbHttp, dbTls, "PUT", dbUrl("/gate/lastCard") + "&print=silent", body, reply);
  sentCardSeq = e.seq;   // each tap is reported once, even if that fails
  if (code < 200 || code >= 300) Serial.printf("[card] could not report the tap to the website (HTTP %d)\n", code);
}

void netTask(void *) {
  dbTls.setCACert(ROOT_CA);
  authTls.setCACert(ROOT_CA);
  uint32_t lastWifiTry = 0, lastPoll = 0, failingSinceMs = 0, setupIdleSinceMs = 0;
  bool timeStarted = false;
  for (;;) {
    uint32_t now = millis();
    if (otaActive) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }   // a firmware update has the network
    if (WiFi.status() != WL_CONNECTED) {
      netLevel = 0;
      if (!cfg.ssid.length()) {
        if (!setupActive && !setupRequest) { setupWhy = "No Wi-Fi saved yet"; setupRequest = 1; }
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }
      if (!failingSinceMs) failingSinceMs = now ? now : 1;
      if (!setupActive && !setupRequest && now - failingSinceMs > SETUP_AFTER_MS) {
        setupWhy = "Could not join the saved Wi-Fi";
        setupRequest = 1;
      }
      // With the setup Wi-Fi open, retry rarely and never while a laptop is using the setup page
      // (a connection attempt makes the ESP32 hop channels, which drops the laptop).
      bool someoneOnSetup = setupActive && WiFi.softAPgetStationNum() > 0;
      uint32_t gap = setupActive ? 60000 : 10000;
      if (!someoneOnSetup && (!lastWifiTry || now - lastWifiTry > gap)) {
        lastWifiTry = now;
        Serial.printf("[wifi] connecting to %s...\n", cfg.ssid.c_str());
        WiFi.disconnect();
        WiFi.begin(cfg.ssid.c_str(), cfg.wifiPass.c_str());
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    failingSinceMs = 0;
    // Close the setup Wi-Fi once everything works and nobody has used it for 2 minutes.
    if (setupActive && netLevel == 2 && WiFi.softAPgetStationNum() == 0) {
      if (!setupIdleSinceMs) setupIdleSinceMs = now;
      else if (now - setupIdleSinceMs > 120000 && !setupRequest) setupRequest = 2;
    } else {
      setupIdleSinceMs = 0;
    }
    if (!timeStarted) {
      timeStarted = true;
      Serial.printf("[wifi] connected, IP %s, signal %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      configTime(0, 0, "pool.ntp.org", "time.google.com");   // needed to check command age and certificates
    }
    if (!timeSynced()) { netLevel = 1; vTaskDelay(pdMS_TO_TICKS(250)); continue; }

    if (!cfg.devicePass.length()) {
      static bool told = false;
      if (!told) { told = true; Serial.println("[firebase] no ESP32 account password yet: enter it on the setup page"); }
      netLevel = 1;
      if (!setupActive && !setupRequest) { setupWhy = "The ESP32 account password is missing"; setupRequest = 1; }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!idToken.length() || (int32_t)(now - tokenRefreshAtMs) >= 0) {
      if ((int32_t)(now - authRetryAtMs) < 0) { vTaskDelay(pdMS_TO_TICKS(250)); continue; }
      if (!firebaseSignIn()) {
        netLevel = 1;
        idToken = "";
        authRetryAtMs = now + (authLocked ? AUTH_LOCKED_WAIT_MS : authBackoffMs);
        authBackoffMs = min(authBackoffMs * 2, (uint32_t)60000);
        continue;
      }
    }
    if ((int32_t)(now - pauseUntilMs) < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    if (sseUp && sseToken != idToken) sseClose("new sign-in token");   // reopen with the fresh token
    if (!sseUp && (int32_t)(now - sseRetryAtMs) >= 0) {
      sseUp = sseOpen();
      if (!sseUp) {                                                   // keep polling meanwhile; try again later
        sseRetryAtMs = millis() + sseBackoffMs;
        sseBackoffMs = min(sseBackoffMs * 2, (uint32_t)30000);
      }
    }
    if (sseUp) ssePump();
    else if (now - lastPoll >= CMD_POLL_MS) { lastPoll = now; pollCommand(); }
#if RFID_ENABLED
    static uint32_t lastCardsSyncMs = 0;
    if (cardsSyncNow || millis() - lastCardsSyncMs >= CARDS_SYNC_MS) { cardsSyncNow = false; lastCardsSyncMs = millis(); syncCards(); }
    maybeSendCard();
#endif
    if ((int32_t)(millis() - pauseUntilMs) >= 0) maybeSendState();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

/* ================================================================ local network: status page, Wi-Fi setup, Wi-Fi updates (core 0) */

static WebServer web(80);
static DNSServer dns;
static bool otaOn = false;
static uint32_t setupSinceMs = 0;
static volatile bool restartPending = false;
static uint32_t restartAtMs = 0;

static const char STATUS_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Smart gate</title><style>
:root{color-scheme:light dark;--bg:#f4f7fb;--card:#fff;--fg:#0f172a;--mut:#64748b;--line:#dfe6ef;--ok:#11744a;--bad:#bb2329;--warn:#8a5e00}
@media(prefers-color-scheme:dark){:root{--bg:#0a1420;--card:#101d2e;--fg:#e5edf7;--mut:#8b9bb0;--line:#22364f;--ok:#6fd9a5;--bad:#ff9195;--warn:#f6cd6a}}
body{margin:0;font:15px system-ui,"Segoe UI",sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:520px;margin:0 auto;padding:16px}h1{font-size:20px;margin:4px 0 2px}p{color:var(--mut);margin:0 0 14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:0 16px}
.row{display:flex;justify-content:space-between;gap:12px;padding:11px 0;border-top:1px solid var(--line)}.row:first-child{border-top:0}
.row span{color:var(--mut)}.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}
small{display:block;margin-top:12px;color:var(--mut);line-height:1.5}
</style></head><body><main><h1>Smart Parking gate</h1><p>Straight from the ESP32 &middot; view only</p>
<div class="card" id="c"></div><small id="m">Connecting&hellip;</small></main><script>
const R=[["Gate",s=>[s.gate.toUpperCase(),s.gate=="open"?"ok":s.gate=="closed"?"bad":"warn"]],
["Servo angle",s=>[s.angle+"°"]],["Mode",s=>[s.mode=="manual"?"MANUAL":"AUTOMATIC"]],
["Vehicle",s=>[s.vehicle?"DETECTED":"NOT DETECTED",s.vehicle?"warn":""]],
["Distance",s=>s.sensorOk?[s.distanceCm==null?"Out of range":s.distanceCm.toFixed(1)+" cm"]:["Sensor fault","bad"]],
["Emergency stop",s=>[s.estop?"ACTIVE":"Off",s.estop?"bad":""]],
["Clearance sensor",s=>[!s.clearSensor?"Not installed":s.laneBusy?"Vehicle in lane":"Clear"]],
["Website link",s=>[["Offline","Wi-Fi only","Online"][s.net],["bad","warn","ok"][s.net]]],
["Wi-Fi updates",s=>s.ota=="ready"?["Ready","ok"]:s.ota=="busy"?["Only while the gate is closed","warn"]:["Off: no update password"]]];
async function tick(){try{const s=await(await fetch("/status.json",{cache:"no-store"})).json();const c=document.getElementById("c");c.textContent="";
for(const[l,f]of R){const[v,k]=f(s),r=document.createElement("div"),a=document.createElement("span"),b=document.createElement("b");
r.className="row";a.textContent=l;b.textContent=v;if(k)b.className=k;r.append(a,b);c.append(r);}
document.getElementById("m").textContent=(s.note?s.note+" · ":"")+"Firmware "+s.fw+" · "+s.ip+" · Wi-Fi "+s.rssi+" dBm · up "+Math.floor(s.uptimeS/60)+" min";}
catch(e){document.getElementById("m").textContent="Lost contact with the ESP32. Is it on the same Wi-Fi?";}}
tick();setInterval(tick,1000);</script></body></html>)HTML";

// Shown only to devices joined to the ESP32's own setup Wi-Fi. Passwords are sent, never shown back.
static const char SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Smart gate setup</title><style>
:root{color-scheme:light dark;--bg:#f4f7fb;--card:#fff;--fg:#0f172a;--mut:#64748b;--line:#dfe6ef;--acc:#1f6fe0;--bad:#bb2329;--ok:#11744a}
@media(prefers-color-scheme:dark){:root{--bg:#0a1420;--card:#101d2e;--fg:#e5edf7;--mut:#8b9bb0;--line:#22364f;--acc:#4b8ff2;--bad:#ff9195;--ok:#6fd9a5}}
body{margin:0;font:15px system-ui,"Segoe UI",sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:480px;margin:0 auto;padding:16px}h1{font-size:20px;margin:4px 0 4px}p{color:var(--mut);margin:0 0 6px}
form{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:4px 16px 16px;margin-top:12px}
label{display:block;font-weight:600;margin:14px 0 6px}
input,select{width:100%;box-sizing:border-box;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--card);color:var(--fg);font:inherit}
.hint{color:var(--mut);font-size:13px;margin-top:5px}
.go{margin-top:18px;width:100%;padding:12px;border:0;border-radius:9px;background:var(--acc);color:#fff;font:inherit;font-weight:700;cursor:pointer}
.link{background:none;border:0;color:var(--mut);text-decoration:underline;font:inherit;font-size:13px;margin-top:14px;cursor:pointer;padding:0}
#msg{font-weight:600;margin-top:14px}#msg.bad{color:var(--bad)}#msg.ok{color:var(--ok)}
</style></head><body><main><h1>Smart Parking gate &middot; Wi-Fi setup</h1><p id="cur">Loading&hellip;</p>
<form id="f" autocomplete="off">
<label for="net">Wi-Fi network</label><select id="net"><option value="">Searching for networks&hellip;</option></select>
<input id="ssid" name="ssid" maxlength="32" placeholder="or type the network name" style="margin-top:8px">
<div class="hint">2.4 GHz networks only. A phone hotspot works too.</div>
<label for="wpass">Wi-Fi password</label><input id="wpass" name="wpass" type="password" maxlength="63">
<label for="dpass">ESP32 account password</label><input id="dpass" name="dpass" type="password" maxlength="100">
<div class="hint" id="dh"></div>
<label for="opass">Update password (optional)</label><input id="opass" name="opass" type="password" maxlength="64">
<div class="hint">For uploading new firmware over Wi-Fi later. At least 8 characters. Leave empty to keep the saved one.</div>
<button class="go">Save and connect</button><p id="msg" role="status"></p>
<button type="button" class="link" id="forget">Forget the saved Wi-Fi and passwords</button>
</form></main><script>
const $=i=>document.getElementById(i),say=(t,k)=>{$("msg").textContent=t;$("msg").className=k||""};
fetch("/setup.json").then(r=>r.json()).then(s=>{
$("cur").textContent=s.ssid?"Saved Wi-Fi: "+s.ssid+(s.wifi?" (connected)":" (could not connect)"):"No Wi-Fi saved yet.";
$("dh").textContent="The password of "+s.email+" in Firebase. "+(s.devicePass?"Leave empty to keep the saved one.":"Required.");
$("dpass").required=!s.devicePass;});
fetch("/scan").then(r=>r.json()).then(list=>{const n=$("net");n.textContent="";n.add(new Option("Choose a network…",""));
list.sort((a,b)=>b.rssi-a.rssi);for(const w of list)n.add(new Option(w.ssid+"  ("+(w.rssi>-60?"strong":w.rssi>-75?"good":"weak")+(w.open?", open":"")+")",w.ssid));})
.catch(()=>{const n=$("net");n.textContent="";n.add(new Option("Could not search: type the name below",""));});
$("net").onchange=()=>{if($("net").value)$("ssid").value=$("net").value;};
$("f").onsubmit=async e=>{e.preventDefault();const b=new URLSearchParams(new FormData($("f")));
if(!b.get("ssid")){say("Choose or type a Wi-Fi network.","bad");return;}
say("Saving…");try{const r=await(await fetch("/save",{method:"POST",body:b})).json();say(r.message,r.ok?"ok":"bad");}
catch(x){say("Saved. The ESP32 is restarting.","ok");}};
$("forget").onclick=async()=>{if(!confirm("Forget the saved Wi-Fi and passwords?"))return;
try{const r=await(await fetch("/forget",{method:"POST"})).json();say(r.message,"ok");}catch(x){say("Done. The ESP32 is restarting.","ok");}};
</script></body></html>)HTML";

static bool otaPasswordOk() { return cfg.otaPass.length() >= 8; }

static const char *otaState() {
  if (!OTA_ENABLED || !otaOn) return "off";
  return otaAllowed ? "ready" : "busy";
}

// True when the browser is on the ESP32's own setup Wi-Fi (not your normal network).
static bool fromSetupWifi() { return setupActive && web.client().localIP() == WiFi.softAPIP(); }

static void sendJson(int code, const String &json) {
  web.sendHeader("Cache-Control", "no-store");
  web.send(code, "application/json", json);
}

static void handleStatus() {
  Snapshot s;
  portENTER_CRITICAL(&shareMux);
  s = shared;
  portEXIT_CRITICAL(&shareMux);
  sendJson(200, stateJson(s) + ",\"rssi\":" + String(WiFi.RSSI()) + ",\"uptimeS\":" + String(millis() / 1000) +
                ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"net\":" + String(netLevel) +
                ",\"ota\":\"" + otaState() + "\"}");
}

static void handleSetupInfo() {
  if (!fromSetupWifi()) { web.send(403, "text/plain", "Only from the setup Wi-Fi"); return; }
  sendJson(200, String("{\"ssid\":\"") + jsonEscape(cfg.ssid.c_str()) + "\",\"wifi\":" +
                (WiFi.status() == WL_CONNECTED ? "true" : "false") + ",\"devicePass\":" +
                (cfg.devicePass.length() ? "true" : "false") + ",\"email\":\"" + jsonEscape(DEVICE_EMAIL) + "\"}");
}

static void handleScan() {
  if (!fromSetupWifi()) { web.send(403, "text/plain", "Only from the setup Wi-Fi"); return; }
  int n = WiFi.scanNetworks();
  String j = "[";
  for (int i = 0; i < n; i++) {
    String name = WiFi.SSID(i);
    if (!name.length()) continue;
    String quoted = "\"" + jsonEscape(name.c_str()) + "\"";
    if (j.indexOf(quoted) >= 0) continue;   // the same network seen on two access points
    if (j.length() > 1) j += ",";
    j += "{\"ssid\":" + quoted + ",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"open\":" +
         (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  WiFi.scanDelete();
  sendJson(200, j + "]");
}

static void setupReply(bool ok, const String &msg) {
  sendJson(ok ? 200 : 400, String("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" + jsonEscape(msg.c_str()) + "\"}");
}

// Checks and saves new settings (from the setup page or the USB cable). They apply after a restart.
static bool saveSettings(String ssid, const String &wpass, const String &dpass, const String &opass, String &msg) {
  ssid.trim();
  if (!ssid.length() || ssid.length() > 32) { msg = "Choose or type a Wi-Fi network."; return false; }
  if (wpass.length() && (wpass.length() < 8 || wpass.length() > 63)) { msg = "Wi-Fi passwords have 8 to 63 characters. Check it again."; return false; }
  if (!dpass.length() && !cfg.devicePass.length()) { msg = "Enter the ESP32 account password."; return false; }
  if (opass.length() && opass.length() < 8) { msg = "The update password needs at least 8 characters."; return false; }
  Preferences p;
  if (!p.begin("gate", false)) { msg = "Could not save on the ESP32 (storage error)."; return false; }
  p.putString("ssid", ssid);
  p.putString("wpass", wpass);
  if (dpass.length()) p.putString("dpass", dpass);
  if (opass.length()) p.putString("opass", opass);
  p.end();
  Serial.printf("[setup] saved Wi-Fi \"%s\"; restarting to use it\n", ssid.c_str());
  restartPending = true;
  restartAtMs = millis() + 2000;
  msg = String("Saved. The ESP32 restarts and joins \"") + ssid + "\"" + (otaAllowed ? "." : " as soon as the gate is closed.");
  return true;
}

static void handleSave() {
  if (!fromSetupWifi()) { web.send(403, "text/plain", "Only from the setup Wi-Fi"); return; }
  String msg;
  bool ok = saveSettings(web.arg("ssid"), web.arg("wpass"), web.arg("dpass"), web.arg("opass"), msg);
  setupReply(ok, ok ? msg + " Now reconnect this laptop to your normal Wi-Fi and open the website." : msg);
}

// Settings over the USB cable, sent by esp32/setup-wifi.ps1 as one line:
//   GATESET <ssid> <wifi password> <ESP32 account password> <update password>
// Each value is base64 (so spaces and symbols survive); "-" means empty. Passwords are never printed.
static String b64decode(const String &in) {
  if (in == "-") return String();
  size_t outLen = 0;
  unsigned char buf[160];
  if (mbedtls_base64_decode(buf, sizeof(buf) - 1, &outLen, (const unsigned char *)in.c_str(), in.length()) != 0) return String();
  buf[outLen] = 0;
  return String((const char *)buf);
}

static void handleSerialLine(String line) {
  line.trim();
  if (line == "GATEINFO") {
    Serial.printf("[setup] INFO wifi=\"%s\" connected=%s devicePassword=%s updatePassword=%s\n", cfg.ssid.c_str(),
                  WiFi.status() == WL_CONNECTED ? "yes" : "no", cfg.devicePass.length() ? "set" : "missing",
                  cfg.otaPass.length() ? "set" : "missing");
    return;
  }
  if (!line.startsWith("GATESET ")) return;
  String part[4];
  int from = 8;
  for (int i = 0; i < 4; i++) {
    int sp = line.indexOf(' ', from);
    part[i] = line.substring(from, sp < 0 ? line.length() : sp);
    from = sp < 0 ? line.length() : sp + 1;
  }
  String msg;
  bool ok = saveSettings(b64decode(part[0]), b64decode(part[1]), b64decode(part[2]), b64decode(part[3]), msg);
  Serial.printf("[setup] %s %s\n", ok ? "OK" : "ERROR", msg.c_str());
}

static void pollSerialSetup() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (buf.length()) handleSerialLine(buf);
      buf = "";
    } else if (buf.length() < 600) {
      buf += c;
    }
  }
}

static void handleForget() {
  if (!fromSetupWifi()) { web.send(403, "text/plain", "Only from the setup Wi-Fi"); return; }
  Preferences p;
  p.begin("gate", false);
  p.clear();
  p.end();
  Serial.println("[setup] saved settings cleared; restarting");
  restartPending = true;
  restartAtMs = millis() + 2000;
  setupReply(true, "Forgotten. The ESP32 restarts and opens this setup Wi-Fi again.");
}

static void startWeb() {
  web.on("/", HTTP_GET, []() {
    if (fromSetupWifi()) web.send(200, "text/html", SETUP_PAGE);
    else if (STATUS_PAGE_ENABLED) web.send(200, "text/html", STATUS_PAGE);
    else web.send(404, "text/plain", "Not found");
  });
  web.on("/status.json", HTTP_GET, handleStatus);
  web.on("/setup.json", HTTP_GET, handleSetupInfo);
  web.on("/scan", HTTP_GET, handleScan);
  web.on("/save", HTTP_POST, handleSave);
  web.on("/forget", HTTP_POST, handleForget);
  web.onNotFound([]() {
    // On the setup Wi-Fi every address leads to the setup page, so laptops and phones open it by themselves.
    if (fromSetupWifi()) {
      web.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
      web.send(302, "text/plain", "");
    } else {
      web.send(404, "text/plain", "Not found");
    }
  });
  web.begin();
}

static void startSetupAp(const char *why) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_NAME, SETUP_AP_PASSWORD);
  vTaskDelay(pdMS_TO_TICKS(200));
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  setupSinceMs = millis();
  setupActive = true;
  Serial.printf("[setup] %s. On the laptop, join the Wi-Fi \"%s\" (password: %s); the setup page opens,\n"
                "        or go to http://%s\n", why, SETUP_AP_NAME, SETUP_AP_PASSWORD, WiFi.softAPIP().toString().c_str());
}

static void stopSetupAp() {
  setupActive = false;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  Serial.println("[setup] connected to the saved Wi-Fi; setup Wi-Fi closed");
}

// OTA and the .local name need the normal Wi-Fi, so they start the first time it connects.
static void startNetServices() {
  bool mdnsUp = false;
#if OTA_ENABLED
  if (otaPasswordOk()) {
    ArduinoOTA.setHostname(DEVICE_HOSTNAME);
    ArduinoOTA.setPassword(cfg.otaPass.c_str());
    ArduinoOTA.onStart([]() { otaActive = true; Serial.println("[ota] update started; the barrier stays down until the restart"); });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
      static int lastTen = -1;
      int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
      if (pct / 10 != lastTen) { lastTen = pct / 10; Serial.printf("[ota] %d%%\n", pct); }
    });
    ArduinoOTA.onEnd([]() { Serial.println("[ota] finished, restarting"); });
    ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] update failed (error %u)\n", (unsigned)e); otaActive = false; });
    ArduinoOTA.begin();   // also announces the board on the network as DEVICE_HOSTNAME.local
    otaOn = mdnsUp = true;
    Serial.printf("[ota] Wi-Fi updates on: network port \"%s\" (only while the gate is closed)\n", DEVICE_HOSTNAME);
  } else {
    Serial.println("[ota] Wi-Fi updates off: no update password (set one on the setup page or in secrets.h)");
  }
#endif
  if (!mdnsUp) mdnsUp = MDNS.begin(DEVICE_HOSTNAME);
  if (mdnsUp) MDNS.addService("http", "tcp", 80);
#if STATUS_PAGE_ENABLED
  Serial.printf("[web] status page: http://%s.local  or  http://%s\n", DEVICE_HOSTNAME, WiFi.localIP().toString().c_str());
#endif
}

// Runs the web pages, the setup Wi-Fi and Wi-Fi updates. It is separate from the Firebase task, so a
// slow HTTPS call never makes a page or an update hang.
void localTask(void *) {
  startWeb();
  bool netServicesStarted = false;
  for (;;) {
    uint8_t req = setupRequest;
    setupRequest = 0;
    if (req == 1 && !setupActive) startSetupAp(setupWhy);
    else if (req == 2 && setupActive) stopSetupAp();
    if (setupActive) dns.processNextRequest();
    if (WiFi.status() == WL_CONNECTED && !netServicesStarted) { netServicesStarted = true; startNetServices(); }
#if OTA_ENABLED
    if (otaOn && (otaAllowed || otaActive)) ArduinoOTA.handle();
#endif
    web.handleClient();
    pollSerialSetup();
    // New settings take effect after a restart, which only happens while the gate is closed and idle.
    if (restartPending && otaAllowed && (int32_t)(millis() - restartAtMs) >= 0) {
      Serial.println("[setup] restarting with the new settings");
      vTaskDelay(pdMS_TO_TICKS(200));
      ESP.restart();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
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
  {
    uint32_t t0 = micros();
    for (int i = 0; i < 5; i++) servoWrite(gate.angle);
    Serial.printf("[servo] one signal update takes %lu us\n", (unsigned long)((micros() - t0) / 5));
  }
  servoWrite(gate.angle);       // start with the barrier down
  sonarBegin(entrance, PIN_TRIG, PIN_ECHO);
#if CLEAR_SENSOR_ENABLED
  sonarBegin(clearance, PIN_CLEAR_TRIG, PIN_CLEAR_ECHO);
#endif
  rfidBegin();
  loadCardCache();
  snprintf(bootId, sizeof(bootId), "%08lx", (unsigned long)esp_random());
  setNote("Gate closed");
  publishSnapshot();

  loadSettings();
  if (cfg.ssid.length()) Serial.printf("[setup] saved Wi-Fi: %s\n", cfg.ssid.c_str());
  else Serial.println("[setup] no Wi-Fi saved yet");

  commandQueue = xQueueCreate(8, sizeof(Command));
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // faster replies to the website
  WiFi.setAutoReconnect(false);  // the network task retries itself, so the setup Wi-Fi stays stable
  servoWantDeg = gate.angle;
  xTaskCreatePinnedToCore(servoTask, "servo", 3072, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(localTask, "local", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
  static uint32_t lastPingMs = 0;
  static bool pingClear = false;
  uint32_t now = millis();

  // While new firmware is written over Wi-Fi the barrier stays down (updates only start when it is closed).
  if (otaActive) { digitalWrite(PIN_STATUS_LED, (now / 50) % 2); delay(5); return; }

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
  // While the barrier moves (and briefly after), missing echoes are ignored: see sonarPoll().
  bool servoBusy = gate.angle != gate.target || now - lastServoStepMs < 400;
  if (sonarPoll(entrance, servoBusy)) detectorUpdate(vehicle, entrance.fault ? NAN : entrance.cm, DETECT_MIN_CM, DETECT_CM, DETECT_CM + RELEASE_HYSTERESIS_CM);

  // Distance in the Serial Monitor (at most twice a second, only when it changes), for checking the wiring.
  static uint32_t lastPrintMs = 0;
  static float lastPrinted = -2;
  float shown = entrance.fault ? -1 : isnan(entrance.cm) ? 0 : entrance.cm;
  if (now - lastPrintMs >= 500 && fabsf(shown - lastPrinted) >= 1.0f) {
    lastPrintMs = now;
    lastPrinted = shown;
    if (entrance.fault) Serial.println("[sensor] no echo at all: check the HC-SR04 wiring and its 5 V");
    else if (isnan(entrance.cm)) Serial.println("[sensor] nothing in range");
    else Serial.printf("[sensor] %.1f cm%s\n", entrance.cm, vehicle.on ? "  (vehicle detected)" : "");
  }
#if CLEAR_SENSOR_ENABLED
  if (sonarPoll(clearance, servoBusy)) detectorUpdate(laneBusy, clearance.fault ? NAN : clearance.cm, 0, CLEAR_DETECT_CM, CLEAR_DETECT_CM + RELEASE_HYSTERESIS_CM);
#endif

  if (echoLostWhileMoving) {
    static bool told = false;
    if (!told) {
      told = true;
      Serial.println("[sensor] no echo while the servo moves: the servo may be pulling the sensor's 5 V down. Give the servo its own 5 V supply.");
    }
  }

  // RFID card reader.
  rfidPoll(now);

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
  otaAllowed = gate.state == G_CLOSED && gate.angle == gate.target && !gate.estop && !vehicle.on && !entrance.fault;

  // 5. Status LED: steady = online, slow blink = connecting, short flash = setup Wi-Fi open,
  //    fast blink = emergency stop.
  bool led = gate.estop ? (now / 100) % 2 : netLevel == 2 ? true : setupActive ? (now % 1500) < 150 : (now / 500) % 2;
  digitalWrite(PIN_STATUS_LED, led);

  delay(1);
}
