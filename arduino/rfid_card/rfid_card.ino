/*
 * Smart Parking: RFID card gate (Arduino UNO + RC522 reader + servo)
 * ------------------------------------------------------------------
 * A registered card opens the gate: the arm turns from 0 to 90 degrees, stays open for a few
 * seconds, then closes again. Unknown cards are refused. Registered cards are kept in the UNO's
 * EEPROM, so they survive power-off and new uploads. Works on its own: no Wi-Fi needed.
 *
 * Libraries: MFRC522 by GithubCommunity (Tools > Manage Libraries > "MFRC522") and Servo (built in).
 * Board:     Arduino UNO (Tools > Board > Arduino AVR Boards > Arduino Uno).
 *
 * Wiring (RC522 -> UNO):
 *   SDA  -> D10      SCK  -> D13      MOSI -> D11      MISO -> D12
 *   RST  -> D9       GND  -> GND      3.3V -> 3.3V (NOT 5V)      IRQ -> not connected
 * Servo -> UNO: signal (orange/yellow) -> D3, + (red) -> 5V, - (brown/black) -> GND.
 *   If the UNO restarts when the servo moves, power the servo from an external 5 V supply instead
 *   (supply + to servo red, supply - to servo brown AND to a UNO GND pin).
 * Optional: green LED (with 220 ohm) on D7, red LED on D6, buzzer on D5.
 *
 * Use it from the Serial Monitor at 115200 baud (line ending: Newline):
 *   add     the next card you tap is registered as allowed
 *   remove  the next card you tap is removed
 *   list    shows the registered cards
 *   clear   removes all cards
 *   open    opens the gate once (to test the servo)
 *   test    checks the RC522 reader
 *   help    shows these commands
 * Tapping a card otherwise prints ACCESS GRANTED (and opens the gate) or ACCESS DENIED.
 *
 * "360 degree" (continuous-rotation) servos, CONTINUOUS_SERVO 1: they can't be sent to an angle, only
 * told to turn, so 90 degrees = turning for TURN_90_MS and back. Put the arm at the closed spot before
 * power-on. To find the time:
 *   fwd 400 / back 400   turn forward / back for 400 ms
 *   turn90 400           use 400 ms for the gate until the next upload (then set TURN_90_MS)
 *   stop                 stop; the arm's position now counts as closed
 * The arm can drift a little over many openings; nudge it back with fwd/back + stop. A normal 180-degree
 * servo (CONTINUOUS_SERVO 0) goes to exactly 0 and 90 degrees every time.
 */
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <Servo.h>

const uint8_t PIN_SS = 10;        // RC522 SDA (chip select)
const uint8_t PIN_RST = 9;        // RC522 RST
const uint8_t PIN_GREEN = 7;      // optional
const uint8_t PIN_RED = 6;        // optional
const uint8_t PIN_BUZZER = 5;     // optional
const uint8_t PIN_SERVO = 3;      // gate servo signal

// Gate (servo) settings
// Which servo is it? 1 = "360 degree" continuous-rotation servo (it can only be told a speed and a
// direction, not an angle), 0 = normal 180-degree servo (goes exactly to the angles below).
#define CONTINUOUS_SERVO 1
const unsigned long GATE_OPEN_MS = 5000;    // how long the gate stays open after a card

// Normal 180-degree servo
const int GATE_CLOSED_DEG = 0;
const int GATE_OPEN_DEG = 90;
const unsigned long SERVO_STEP_MS = 8;      // ms per degree: 90 degrees in about 0.7 s

// 360-degree servo: open = turn one way for TURN_90_MS, then stop; close = turn back for the same time.
// Find the right time with the "fwd 250" / "back 250" commands (milliseconds), then set it here.
const int SERVO_STOP_US = 1500;             // pulse that means "stop"
const int SERVO_SPEED_US = 200;             // 1500 +/- 200 us = medium speed
unsigned long TURN_90_MS = 400;             // how long a quarter turn (90 degrees) takes (measured 2026-11-13)
const int OPEN_DIRECTION = 1;               // -1 if the arm opens the wrong way

const uint8_t MAX_CARDS = 20;
const uint8_t UID_MAX = 10;       // RC522 UIDs are 4, 7 or 10 bytes
const uint8_t EE_MAGIC = 0xC4;    // marks the EEPROM as ours
const int EE_COUNT = 1, EE_CARDS = 2, EE_SLOT = 1 + UID_MAX;   // slot = length byte + UID bytes
const unsigned long WAIT_MS = 15000;                            // how long add/remove waits for a tap
// Receiver gain. This RC522 (chip version 0x82) garbles card replies at medium or high gain and only
// reads reliably at the lowest gain. If cards are not detected, run "test" and pick the gain that answers.
const byte READER_GAIN = MFRC522::RxGain_min;

MFRC522 reader(PIN_SS, PIN_RST);
Servo gateServo;

enum Mode : uint8_t { NORMAL, ADDING, REMOVING };
Mode mode = NORMAL;
unsigned long modeSince = 0;
String lastUid;
unsigned long lastTapMs = 0;

/* ---------------------------------------------------------------- stored cards (EEPROM) */

uint8_t cardCount() {
  if (EEPROM.read(0) != EE_MAGIC) { EEPROM.update(0, EE_MAGIC); EEPROM.update(EE_COUNT, 0); }
  uint8_t n = EEPROM.read(EE_COUNT);
  return n > MAX_CARDS ? 0 : n;
}

String uidText(const byte *uid, byte len) {
  String s;
  for (byte i = 0; i < len; i++) {
    if (i) s += ':';
    if (uid[i] < 0x10) s += '0';
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

String storedUid(uint8_t slot) {
  int base = EE_CARDS + slot * EE_SLOT;
  byte len = EEPROM.read(base);
  byte uid[UID_MAX];
  if (len > UID_MAX) len = 0;
  for (byte i = 0; i < len; i++) uid[i] = EEPROM.read(base + 1 + i);
  return uidText(uid, len);
}

int findCard(const String &uid) {
  uint8_t n = cardCount();
  for (uint8_t i = 0; i < n; i++) if (storedUid(i) == uid) return i;
  return -1;
}

bool addCard(const byte *uid, byte len) {
  uint8_t n = cardCount();
  if (n >= MAX_CARDS) return false;
  int base = EE_CARDS + n * EE_SLOT;
  EEPROM.update(base, len);
  for (byte i = 0; i < len; i++) EEPROM.update(base + 1 + i, uid[i]);
  EEPROM.update(EE_COUNT, n + 1);
  return true;
}

void removeCard(int slot) {
  uint8_t n = cardCount();
  for (int s = slot; s < n - 1; s++) {               // move the later cards up one slot
    for (int b = 0; b < EE_SLOT; b++) EEPROM.update(EE_CARDS + s * EE_SLOT + b, EEPROM.read(EE_CARDS + (s + 1) * EE_SLOT + b));
  }
  EEPROM.update(EE_COUNT, n - 1);
}

void listCards() {
  uint8_t n = cardCount();
  Serial.print(F("[rfid] ")); Serial.print(n); Serial.println(n == 1 ? F(" card registered") : F(" cards registered"));
  for (uint8_t i = 0; i < n; i++) { Serial.print(F("[rfid]   ")); Serial.print(i + 1); Serial.print(F(") ")); Serial.println(storedUid(i)); }
}

/* ---------------------------------------------------------------- gate (servo), moved without blocking */

#if CONTINUOUS_SERVO

// The servo turns only while it gets a signal; between moves the signal is switched off, so it
// cannot creep. Opening while it is still closing turns back for the time it already ran.
enum GateState : uint8_t { G_CLOSED, G_OPENING, G_OPEN, G_CLOSING, G_TEST };
GateState gateState = G_CLOSED;
unsigned long runStartMs = 0, runEndMs = 0, closeAtMs = 0;

void runServo(int direction, unsigned long ms) {
  if (!gateServo.attached()) gateServo.attach(PIN_SERVO);
  gateServo.writeMicroseconds(SERVO_STOP_US + direction * OPEN_DIRECTION * SERVO_SPEED_US);
  runStartMs = millis();
  runEndMs = runStartMs + ms;
}

void stopServo() {
  gateServo.writeMicroseconds(SERVO_STOP_US);
  delay(20);
  gateServo.detach();
}

void openGate() {
  closeAtMs = millis() + TURN_90_MS + GATE_OPEN_MS;   // another card keeps it open longer
  if (gateState == G_CLOSED) { runServo(+1, TURN_90_MS); gateState = G_OPENING; Serial.println(F("[gate] opening")); }
  else if (gateState == G_CLOSING) {                    // go back up for as long as it already went down
    unsigned long ran = millis() - runStartMs;
    runServo(+1, ran);
    gateState = G_OPENING;
    Serial.println(F("[gate] opening again"));
  }
}

void updateGate() {
  unsigned long now = millis();
  if ((gateState == G_OPENING || gateState == G_CLOSING || gateState == G_TEST) && (long)(now - runEndMs) >= 0) {
    stopServo();
    if (gateState == G_OPENING) { gateState = G_OPEN; Serial.println(F("[gate] open")); }
    else if (gateState == G_CLOSING) { gateState = G_CLOSED; Serial.println(F("[gate] closed")); }
    else { gateState = G_CLOSED; Serial.println(F("[gate] test turn done")); }
  }
  if (gateState == G_OPEN && (long)(now - closeAtMs) >= 0) {
    runServo(-1, TURN_90_MS);
    gateState = G_CLOSING;
    Serial.println(F("[gate] closing"));
  }
}

// "fwd 250" / "back 250": turn for that many ms, to find how long 90 degrees takes.
void testTurn(int direction, unsigned long ms) {
  runServo(direction, ms);
  gateState = G_TEST;
  Serial.print(F("[gate] test turn ")); Serial.print(direction > 0 ? F("forward ") : F("back ")); Serial.print(ms); Serial.println(F(" ms"));
}

#else

int gateAngle = GATE_CLOSED_DEG, gateTarget = GATE_CLOSED_DEG, moveFrom = GATE_CLOSED_DEG;
unsigned long moveStartMs = 0, closeAtMs = 0;

void moveGateTo(int target) {
  if (target == gateTarget) return;
  moveFrom = gateAngle;
  gateTarget = target;
  moveStartMs = millis();
  Serial.println(target == GATE_OPEN_DEG ? F("[gate] opening") : F("[gate] closing"));
}

void openGate() {
  closeAtMs = millis() + abs(GATE_OPEN_DEG - gateAngle) * SERVO_STEP_MS + GATE_OPEN_MS;   // another card keeps it open longer
  moveGateTo(GATE_OPEN_DEG);
}

// Called every loop: moves 1 degree per SERVO_STEP_MS, timed by the clock, and closes when the time is up.
void updateGate() {
  unsigned long now = millis();
  if (gateAngle != gateTarget) {
    int span = abs(gateTarget - moveFrom);
    int done = min((long)((now - moveStartMs) / SERVO_STEP_MS), (long)span);
    int next = moveFrom + (gateTarget > moveFrom ? done : -done);
    if (next != gateAngle) {
      gateAngle = next;
      gateServo.write(gateAngle);
      if (gateAngle == gateTarget) Serial.println(gateAngle == GATE_OPEN_DEG ? F("[gate] open") : F("[gate] closed"));
    }
  }
  if (gateTarget == GATE_OPEN_DEG && gateAngle == GATE_OPEN_DEG && (long)(now - closeAtMs) >= 0) moveGateTo(GATE_CLOSED_DEG);
}

#endif

/* ---------------------------------------------------------------- feedback (LEDs and buzzer, without blocking) */

unsigned long ledOffAtMs = 0;

void signal(bool ok) {
  digitalWrite(ok ? PIN_GREEN : PIN_RED, HIGH);
  if (ok) tone(PIN_BUZZER, 2000, 120); else tone(PIN_BUZZER, 400, 400);
  ledOffAtMs = millis() + (ok ? 400 : 600);
}

void updateLeds() {
  if (ledOffAtMs && (long)(millis() - ledOffAtMs) >= 0) {
    digitalWrite(PIN_GREEN, LOW);
    digitalWrite(PIN_RED, LOW);
    ledOffAtMs = 0;
  }
}

void help() {
  Serial.println(F("[rfid] commands: add | remove | list | clear | open | test | help  (Serial Monitor at 115200, line ending: Newline)"));
#if CONTINUOUS_SERVO
  Serial.println(F("[gate] 360-degree servo: fwd <ms> | back <ms> | turn90 <ms> | stop  (to find the time of a 90 degree turn)"));
#endif
}

// "test": checks the reader step by step. Hold a card on the reader while it runs.
void readerTest() {
  Serial.println(F("[test] reader check (hold a card on the reader now)"));
  reader.PCD_Init();
  reader.PCD_SetAntennaGain(READER_GAIN);
  byte version = reader.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print(F("[test] chip version: 0x")); Serial.println(version, HEX);

  // Write a harmless register and read it back: proves MOSI, SCK and SDA work both ways.
  byte old = reader.PCD_ReadRegister(MFRC522::ModWidthReg);
  reader.PCD_WriteRegister(MFRC522::ModWidthReg, 0x55);
  byte back = reader.PCD_ReadRegister(MFRC522::ModWidthReg);
  reader.PCD_WriteRegister(MFRC522::ModWidthReg, old);
  Serial.print(F("[test] write/read-back: ")); Serial.println(back == 0x55 ? F("OK") : F("FAILED (check MOSI, SCK, SDA wires and soldering)"));

  byte tx = reader.PCD_ReadRegister(MFRC522::TxControlReg);
  Serial.print(F("[test] antenna: ")); Serial.print((tx & 0x03) == 0x03 ? F("ON") : F("OFF"));
  Serial.print(F(" (TxControl 0x")); Serial.print(tx, HEX); Serial.print(F("), gain 0x")); Serial.println(reader.PCD_GetAntennaGain(), HEX);

  // Ask any card in the field to answer, 1.5 s at each antenna gain, counting each kind of result.
  const byte gains[] = { MFRC522::RxGain_min, MFRC522::RxGain_avg, MFRC522::RxGain_max };
  const char *names[] = { "18 dB (low)", "33 dB (medium)", "48 dB (max)" };
  for (byte g = 0; g < 3; g++) {
    reader.PCD_SetAntennaGain(gains[g]);
    int ok = 0, timeout = 0, error = 0, other = 0;
    unsigned long start = millis();
    while (millis() - start < 1500) {
      byte atqa[2];
      byte size = sizeof(atqa);
      MFRC522::StatusCode st = reader.PICC_WakeupA(atqa, &size);
      if (st == MFRC522::STATUS_OK || st == MFRC522::STATUS_COLLISION) ok++;
      else if (st == MFRC522::STATUS_TIMEOUT) timeout++;
      else if (st == MFRC522::STATUS_ERROR) error++;
      else other++;
      delay(30);
    }
    Serial.print(F("[test] gain ")); Serial.print(names[g]);
    Serial.print(F(": card answered ")); Serial.print(ok);
    Serial.print(F(", no answer ")); Serial.print(timeout);
    Serial.print(F(", garbled ")); Serial.print(error);
    Serial.print(F(", other ")); Serial.println(other);
  }
  reader.PCD_SetAntennaGain(READER_GAIN);
  Serial.println(F("[test] done"));
}

/* ---------------------------------------------------------------- setup + loop */

void setup() {
  Serial.begin(115200);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
#if CONTINUOUS_SERVO
  pinMode(PIN_SERVO, OUTPUT);          // no signal = the 360-degree servo stays still
  digitalWrite(PIN_SERVO, LOW);
#else
  gateServo.attach(PIN_SERVO);
  gateServo.write(GATE_CLOSED_DEG);   // start with the gate closed
#endif
  SPI.begin();
  reader.PCD_Init();
  delay(50);
  reader.PCD_SetAntennaGain(READER_GAIN);
  Serial.println();
  Serial.println(CONTINUOUS_SERVO ? F("Smart Parking RFID card gate (UNO + RC522 + 360-degree servo on D3)")
                                  : F("Smart Parking RFID card gate (UNO + RC522 + 180-degree servo on D3)"));

  byte version = reader.PCD_ReadRegister(MFRC522::VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Serial.println(F("[rfid] READER NOT FOUND: check the wiring (SDA->D10, SCK->D13, MOSI->D11, MISO->D12, RST->D9, 3.3V, GND)"));
  } else {
    Serial.print(F("[rfid] reader OK (RC522 version 0x")); Serial.print(version, HEX); Serial.println(F(")"));
  }
  listCards();
  if (!cardCount()) Serial.println(F("[rfid] no cards yet: type 'add' and tap your card to register it"));
  help();
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (!cmd.length()) return;
  if (cmd == "add") {
    mode = ADDING; modeSince = millis();
    Serial.println(F("[rfid] ADD: tap the card to register on the reader (15 s)"));
  } else if (cmd == "remove") {
    mode = REMOVING; modeSince = millis();
    Serial.println(F("[rfid] REMOVE: tap the card to remove on the reader (15 s)"));
  } else if (cmd == "list") {
    listCards();
  } else if (cmd == "clear") {
    EEPROM.update(EE_COUNT, 0);
    Serial.println(F("[rfid] all cards removed"));
  } else if (cmd == "open") {
    openGate();
#if CONTINUOUS_SERVO
  } else if (cmd.startsWith("fwd ") || cmd.startsWith("back ")) {
    long ms = cmd.substring(cmd.indexOf(' ') + 1).toInt();
    if (ms > 0 && ms <= 3000) testTurn(cmd.startsWith("fwd") ? +1 : -1, ms);
    else Serial.println(F("[gate] use e.g. fwd 250 or back 250 (1 to 3000 ms)"));
  } else if (cmd.startsWith("turn90 ")) {
    long ms = cmd.substring(7).toInt();
    if (ms > 0 && ms <= 3000) { TURN_90_MS = ms; Serial.print(F("[gate] a 90 degree turn now takes ")); Serial.print(ms); Serial.println(F(" ms (until the next upload)")); }
  } else if (cmd == "stop") {
    stopServo();
    gateState = G_CLOSED;               // wherever the arm is now counts as closed
    Serial.println(F("[gate] stopped (this position now counts as closed)"));
#endif
  } else if (cmd == "test") {
    readerTest();
  } else if (cmd == "help") {
    help();
  } else {
    Serial.print(F("[rfid] unknown command: ")); Serial.println(cmd);
    help();
  }
}

void loop() {
  updateGate();
  updateLeds();

  // Commands from the Serial Monitor (or the laptop).
  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') { handleCommand(line); line = ""; }
    else if (line.length() < 30) line += c;
  }

  if (mode != NORMAL && millis() - modeSince > WAIT_MS) {
    Serial.println(F("[rfid] no card tapped in time: cancelled"));
    mode = NORMAL;
  }

  // Some RC522 clones stop answering after a while: restart the chip every 5 s while no card is seen.
  static unsigned long lastInitMs = 0;
  if (millis() - lastInitMs > 5000 && millis() - lastTapMs > 5000) {
    lastInitMs = millis();
    reader.PCD_Init();
    reader.PCD_SetAntennaGain(READER_GAIN);
  }

  if (!reader.PICC_IsNewCardPresent() || !reader.PICC_ReadCardSerial()) return;

  String uid = uidText(reader.uid.uidByte, reader.uid.size);
  // The same card held on the reader is read again and again; react once per 1.5 s.
  if (uid == lastUid && millis() - lastTapMs < 1500) { lastTapMs = millis(); reader.PICC_HaltA(); return; }
  lastUid = uid;
  lastTapMs = millis();

  MFRC522::PICC_Type type = reader.PICC_GetType(reader.uid.sak);
  Serial.print(F("[rfid] card ")); Serial.print(uid);
  Serial.print(F(" (")); Serial.print(reader.PICC_GetTypeName(type)); Serial.print(F(")"));

  int slot = findCard(uid);
  if (mode == ADDING) {
    if (slot >= 0) { Serial.println(F(" -> already registered")); signal(true); }
    else if (addCard(reader.uid.uidByte, reader.uid.size)) {
      Serial.print(F(" -> REGISTERED (")); Serial.print(cardCount()); Serial.println(F(" saved)"));
      signal(true);
    } else { Serial.println(F(" -> not saved: the list is full (20 cards)")); signal(false); }
    mode = NORMAL;
  } else if (mode == REMOVING) {
    if (slot >= 0) { removeCard(slot); Serial.println(F(" -> REMOVED")); signal(true); }
    else { Serial.println(F(" -> was not registered")); signal(false); }
    mode = NORMAL;
  } else {
    bool ok = slot >= 0;
    Serial.println(ok ? F(" -> ACCESS GRANTED") : F(" -> ACCESS DENIED (not registered)"));
    signal(ok);
    if (ok) openGate();
  }

  reader.PICC_HaltA();
  reader.PCD_StopCrypto1();
}
