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
 * Home button (360-degree servo): a push button between D2 and GND, placed where the arm presses it
 *   when the gate is closed (0 degrees). On a 4-leg button use two legs at opposite corners.
 * Optional: green LED (with 220 ohm) on D7, red LED on D6, buzzer on D5.
 *
 * Use it from the Serial Monitor at 115200 baud (line ending: Newline):
 *   add     the next card you tap is registered as allowed
 *   remove  the next card you tap is removed
 *   list    shows the registered cards
 *   clear   removes all cards
 *   open    opens the gate once (to test the servo)
 *   closeangle 5 / openangle 95   (180-degree servo) fine-tune the closed / open angle until the next upload
 *   test    checks the RC522 reader
 *   help    shows these commands
 * Tapping a card otherwise prints ACCESS GRANTED (and opens the gate) or ACCESS DENIED.
 *
 * "360 degree" (continuous-rotation) servos, CONTINUOUS_SERVO 1: they can't be sent to an angle, only
 * told to turn, so opening = turning forward for TURN_90_MS. Closing turns back until the arm presses
 * the home button and stops right there, so 0 degrees is exact every time (HOME_BUTTON 1). At power-on
 * the arm turns back slowly to the button. Without a button (HOME_BUTTON 0) it closes by time and can
 * end a few degrees off. Serial commands:
 *   fwd 400 / back 400   turn forward / back for 400 ms
 *   turn90 400           use 400 ms for opening until the next upload (then set TURN_90_MS)
 *   home                 turn back slowly to the home button
 *   stop                 stop; the arm's position now counts as closed
 * A normal 180-degree servo (CONTINUOUS_SERVO 0) goes to exactly 0 and 90 degrees with no button.
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
#define CONTINUOUS_SERVO 1                  // 2026-11-13: the SG90 on the UNO spins all the way round (360-degree type)
const unsigned long GATE_OPEN_MS = 5000;    // how long the gate stays open after a card

// Normal 180-degree servo. If the closed arm isn't exactly level, try "closeangle 5" (or re-seat the
// horn one tooth on the shaft), then put the value that looks right here.
int GATE_CLOSED_DEG = 0;
int GATE_OPEN_DEG = 90;
const unsigned long SERVO_STEP_MS = 8;      // ms per degree: 90 degrees in about 0.7 s

// 360-degree servo: open = turn one way for TURN_90_MS, then stop; close = turn back until the arm
// presses the home button (without a button: turn back for the same time).
// Find the right time with the "fwd 400" / "back 400" commands (milliseconds), then set it here.
const int SERVO_STOP_US = 1500;             // pulse that means "stop"
const int SERVO_SPEED_US = 200;             // 1500 +/- 200 us = medium speed
unsigned long TURN_90_MS = 400;             // how long a quarter turn (90 degrees) takes (measured 2026-11-13)
unsigned long TURN_BACK_MS = 450;           // closing turn: this servo turns back slower than it opens. 400 ms ended
                                            // ~15 deg short, 440 ms a few degrees short after 3 taps (measured 2026-11-13).
                                            // Try other values live with "turnback <ms>".
const int OPEN_DIRECTION = 1;               // -1 if the arm opens the wrong way

// Home button: a push button placed where the closed arm presses it. Closing turns back fast, then
// slowly, and stops the moment the button is pressed, so the gate closes at exactly 0 degrees every time.
#define HOME_BUTTON 0                       // 1 = button on D2 wired; 0 = no button: close by time (TURN_BACK_MS)
const uint8_t PIN_HOME = 2;                 // button between D2 and GND (D2 is an interrupt pin: instant stop)
const int SERVO_SLOW_US = 100;              // 1500 +/- 100 us = slow, for the last part of closing
const unsigned long CLOSE_FAST_PERCENT = 60;  // close fast for 60% of TURN_90_MS, then slowly to the button
const unsigned long HOME_TIMEOUT_MS = 800;    // stop if the slow part hasn't reached the button by then
// (if that happens, the gate closes by time until the button is seen working again)

const uint8_t MAX_CARDS = 20;
const uint8_t UID_MAX = 10;       // RC522 UIDs are 4, 7 or 10 bytes
const uint8_t EE_MAGIC = 0xC4;    // marks the EEPROM as ours
const int EE_COUNT = 1, EE_CARDS = 2, EE_SLOT = 1 + UID_MAX;   // slot = length byte + UID bytes
const unsigned long WAIT_MS = 15000;                            // how long add/remove waits for a tap
// Receiver settings. This RC522 (chip version 0x82) hears noise as a garbled answer on almost every
// poll, even with no card near it, which can hide a real card for a moment (a late gate). Two knobs:
//   gain  0-7: receiver gain, 0 = 18 dB (lowest) ... 7 = 48 dB. Only the lowest read this card at first.
//   level 0-15: the weakest signal the receiver accepts (chip default 8). Higher ignores more noise.
// "scan" tries them all (run it once with no card and once holding the card); "rx <gain> <level>" tries one.
// Scan on 2026-11-13: gain 0 only saw the card when it touched the reader (taps felt late); gain 2 read
// it from a few cm away with no noise, with or without the card; gain 4+ was noisy unless level 14-15.
byte readerGain = 2;
byte readerLevel = 10;

MFRC522 reader(PIN_SS, PIN_RST);
Servo gateServo;

// Starts (or restarts) the RC522 with the receiver settings above.
void readerSetup() {
  reader.PCD_Init();
  reader.PCD_SetAntennaGain(readerGain << 4);
  reader.PCD_WriteRegister(MFRC522::RxThresholdReg, (readerLevel << 4) | 0x04);   // CollLevel 4 = chip default
}

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
// cannot creep. With the home button every close ends exactly on the button (0 degrees), so the
// small error of a timed turn never adds up. Opening while it is still closing turns back up.
enum GateState : uint8_t { G_CLOSED, G_OPENING, G_OPEN, G_CLOSING, G_HOMING, G_TEST };
GateState gateState = G_CLOSED;
unsigned long runStartMs = 0, runEndMs = 0, closeAtMs = 0, closeStartMs = 0;
volatile bool homeWatch = false;    // stop as soon as the home button is pressed
volatile bool homeHit = false;      // the home button stopped the servo
bool homeMissing = false;           // the arm didn't reach the button last time: close by time instead
bool homeShown = false;             // last button state printed
unsigned long homeChangeMs = 0;

bool homePressed() {
#if HOME_BUTTON
  return digitalRead(PIN_HOME) == LOW;
#else
  return false;
#endif
}

// Runs the instant the arm presses the button (interrupt on D2): the servo's next pulse is "stop".
void onHomePressed() {
  if (homeWatch) { gateServo.writeMicroseconds(SERVO_STOP_US); homeWatch = false; homeHit = true; }
}

void runServo(int direction, int speedUs, unsigned long ms) {
  if (!gateServo.attached()) gateServo.attach(PIN_SERVO);
  gateServo.writeMicroseconds(SERVO_STOP_US + direction * OPEN_DIRECTION * speedUs);
  runStartMs = millis();
  runEndMs = runStartMs + ms;
}

void stopServo() {
  homeWatch = false;
  gateServo.writeMicroseconds(SERVO_STOP_US);
  delay(20);
  // Switch the signal off between two pulses. Cutting a pulse short makes the servo jump, and
  // detaching in the middle of one can leave the pin stuck on.
  for (;;) {
    noInterrupts();
    if (digitalRead(PIN_SERVO) == LOW) { gateServo.detach(); interrupts(); break; }
    interrupts();
  }
}

// Turn back slowly until the arm presses the button (at power-on, and the "home" command).
void startHoming() {
  if (homePressed()) { stopServo(); gateState = G_CLOSED; Serial.println(F("[gate] closed: the arm is on the home button (0 degrees)")); return; }
  homeHit = false;
  homeWatch = true;
  runServo(-1, SERVO_SLOW_US, HOME_TIMEOUT_MS);
  gateState = G_HOMING;
}

// Close: back fast for most of the way, then slowly until the arm presses the button.
// If the button wasn't found last time, turn back for the opening time instead (still stopping if it's pressed).
void startClosing() {
  closeStartMs = millis();
#if HOME_BUTTON
  if (homePressed()) { gateState = G_CLOSED; Serial.println(F("[gate] closed (the button was already pressed)")); return; }
  homeHit = false;
  homeWatch = true;
  runServo(-1, SERVO_SPEED_US, homeMissing ? TURN_BACK_MS : TURN_BACK_MS * CLOSE_FAST_PERCENT / 100);
#else
  runServo(-1, SERVO_SPEED_US, TURN_BACK_MS);
#endif
  gateState = G_CLOSING;
  Serial.println(homeMissing ? F("[gate] closing (by time: home button not found)") : F("[gate] closing"));
}

// Report every press and release of the home button (and beep on a press), so the wiring is easy to check.
void watchHomeButton(unsigned long now) {
#if HOME_BUTTON
  bool pressed = homePressed();
  if (pressed == homeShown || now - homeChangeMs < 30) return;
  homeShown = pressed;
  homeChangeMs = now;
  Serial.println(pressed ? F("[gate] home button pressed") : F("[gate] home button released"));
  if (pressed) {
    tone(PIN_BUZZER, 3000, 60);
    if (homeMissing) { homeMissing = false; Serial.println(F("[gate] home button works: closing will stop on it again")); }
  }
#endif
}

void openGate() {
  closeAtMs = millis() + TURN_90_MS + GATE_OPEN_MS;   // another card keeps it open longer
  if (gateState == G_CLOSED) {
    homeWatch = false;
    runServo(+1, SERVO_SPEED_US, TURN_90_MS);
    gateState = G_OPENING;
    Serial.println(F("[gate] opening"));
  } else if (gateState == G_CLOSING || gateState == G_HOMING) {   // go back up about as far as it came down
    unsigned long ran = millis() - runStartMs;
    unsigned long back = gateState == G_CLOSING ? ran
                       : TURN_BACK_MS * CLOSE_FAST_PERCENT / 100 + ran * SERVO_SLOW_US / SERVO_SPEED_US;
    unsigned long up = back * TURN_90_MS / TURN_BACK_MS;   // the same distance, at the opening speed
    homeWatch = false;
    runServo(+1, SERVO_SPEED_US, min(up, TURN_90_MS));
    gateState = G_OPENING;
    Serial.println(F("[gate] opening again"));
  }
}

void updateGate() {
  unsigned long now = millis();
  if (homeWatch && homePressed()) { noInterrupts(); onHomePressed(); interrupts(); }   // in case the edge was missed
  watchHomeButton(now);
  if (homeHit) {                                       // the arm pressed the button: exactly 0 degrees
    homeHit = false;
    stopServo();
    gateState = G_CLOSED;
    Serial.print(F("[gate] closed on the home button (0 degrees, ")); Serial.print(now - closeStartMs); Serial.println(F(" ms)"));
    return;
  }
  if ((gateState == G_OPENING || gateState == G_CLOSING || gateState == G_HOMING || gateState == G_TEST) && (long)(now - runEndMs) >= 0) {
    if (gateState == G_CLOSING && HOME_BUTTON && !homeMissing) {   // fast part done: go on slowly to the button
      runServo(-1, SERVO_SLOW_US, HOME_TIMEOUT_MS);
      gateState = G_HOMING;
      return;
    }
    stopServo();
    if (gateState == G_OPENING) { gateState = G_OPEN; Serial.println(F("[gate] open")); }
    else if (gateState == G_CLOSING) { gateState = G_CLOSED; Serial.println(F("[gate] closed")); }
    else if (gateState == G_HOMING) {
      gateState = G_CLOSED;
      homeMissing = true;
      Serial.print(F("[gate] WARNING: stopped after ")); Serial.print(now - closeStartMs);
      Serial.println(F(" ms without reaching the home button, so the gate closes by time for now. Check the button is on pin 2 and GND where the closed arm presses it (it beeps when pressed), then type 'home'."));
    }
    else { gateState = G_CLOSED; Serial.println(F("[gate] test turn done")); }
  }
  if (gateState == G_OPEN && (long)(now - closeAtMs) >= 0) startClosing();
}

// "fwd 400" / "back 400": turn for that many ms, to find how long 90 degrees takes.
void testTurn(int direction, unsigned long ms) {
  homeWatch = false;
  runServo(direction, SERVO_SPEED_US, ms);
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
  Serial.print(F("[gate] 360-degree servo: opens for ")); Serial.print(TURN_90_MS); Serial.print(F(" ms, closes for ")); Serial.print(TURN_BACK_MS);
  Serial.println(F(" ms. Tune with turn90 <ms> | turnback <ms> | fwd <ms> | back <ms> | stop"));
#if HOME_BUTTON
  Serial.print(F("[gate] home button on D2: ")); Serial.print(homePressed() ? F("pressed") : F("not pressed"));
  Serial.println(F("  (type 'home' to turn back to it)"));
#endif
#else
  Serial.print(F("[gate] 180-degree servo: closed ")); Serial.print(GATE_CLOSED_DEG); Serial.print(F(" deg, open "));
  Serial.print(GATE_OPEN_DEG); Serial.println(F(" deg. Fine-tune with closeangle <deg> | openangle <deg>"));
#endif
}

// "test": checks the reader step by step. Hold a card on the reader while it runs.
void readerTest() {
  Serial.println(F("[test] reader check (hold a card on the reader now)"));
  readerSetup();
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
  readerSetup();
  Serial.println(F("[test] done"));
}

// "scan": for each receiver gain and level, polls for 300 ms and counts clean card answers, silence
// and noise. Run it with no card near the reader, then again holding the card on it: the best setting
// shows "noise 0" without the card and many "card" answers with it.
void readerScan() {
  Serial.println(F("[scan] receiver scan, about 5 s"));
  const byte gains[] = { 0, 2, 4, 7 };
  const byte levels[] = { 8, 10, 12, 14, 15 };
  for (byte gi = 0; gi < sizeof(gains); gi++) {
    for (byte li = 0; li < sizeof(levels); li++) {
      reader.PCD_SetAntennaGain(gains[gi] << 4);
      reader.PCD_WriteRegister(MFRC522::RxThresholdReg, (levels[li] << 4) | 0x04);
      int card = 0, none = 0, noise = 0;
      unsigned long start = millis();
      while (millis() - start < 300) {
        byte s = cardInField();
        if (s == 2) { card++; reader.PICC_HaltA(); }
        else if (s == 0) none++;
        else noise++;
        delay(2);
      }
      Serial.print(F("[scan] rx ")); Serial.print(gains[gi]); Serial.print(' '); Serial.print(levels[li]);
      Serial.print(F(": card ")); Serial.print(card); Serial.print(F(", nothing ")); Serial.print(none);
      Serial.print(F(", noise ")); Serial.println(noise);
    }
  }
  readerSetup();
  Serial.println(F("[scan] done"));
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
#if HOME_BUTTON
  pinMode(PIN_HOME, INPUT_PULLUP);     // pressed = LOW
  attachInterrupt(digitalPinToInterrupt(PIN_HOME), onHomePressed, FALLING);
#endif
#else
  gateServo.attach(PIN_SERVO);
  gateServo.write(GATE_CLOSED_DEG);   // start with the gate closed
#endif
  SPI.begin();
  readerSetup();
  delay(50);
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
#if CONTINUOUS_SERVO && HOME_BUTTON
  if (!homePressed()) Serial.println(F("[gate] finding 0 degrees: turning back slowly to the home button"));
  closeStartMs = millis();
  startHoming();                       // start every power-on at exactly 0 degrees
#endif
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
  } else if (cmd.startsWith("turnback ")) {
    long ms = cmd.substring(9).toInt();
    if (ms > 0 && ms <= 3000) { TURN_BACK_MS = ms; Serial.print(F("[gate] closing now turns back for ")); Serial.print(ms); Serial.println(F(" ms (until the next upload)")); }
  } else if (cmd == "stop") {
    stopServo();
    gateState = G_CLOSED;               // wherever the arm is now counts as closed
    Serial.println(F("[gate] stopped (this position now counts as closed)"));
#if HOME_BUTTON
  } else if (cmd == "home") {
    stopServo();
    closeStartMs = millis();
    Serial.println(F("[gate] turning back slowly to the home button"));
    startHoming();
#endif
#else
  } else if (cmd.startsWith("closeangle ") || cmd.startsWith("openangle ")) {
    // Fine-tune an angle until the next upload; the arm moves there right away if it's at that end.
    bool closing = cmd.startsWith("close");
    int deg = cmd.substring(cmd.indexOf(' ') + 1).toInt();
    if (deg < 0 || deg > 180 || cmd.substring(cmd.indexOf(' ') + 1).length() == 0) {
      Serial.println(F("[gate] use an angle from 0 to 180, e.g. closeangle 5"));
    } else {
      int &setting = closing ? GATE_CLOSED_DEG : GATE_OPEN_DEG;
      bool atThatEnd = gateTarget == setting;
      setting = deg;
      if (atThatEnd) moveGateTo(deg);
      Serial.print(closing ? F("[gate] closed angle = ") : F("[gate] open angle = ")); Serial.print(deg);
      Serial.println(F(" deg (until the next upload)"));
    }
#endif
  } else if (cmd == "test") {
    readerTest();
  } else if (cmd == "scan") {
    readerScan();
  } else if (cmd.startsWith("rx ")) {
    int sp = cmd.indexOf(' ', 3);
    int g = cmd.substring(3, sp).toInt(), l = cmd.substring(sp + 1).toInt();
    if (sp > 3 && g >= 0 && g <= 7 && l >= 0 && l <= 15) {
      readerGain = g; readerLevel = l; readerSetup();
      Serial.print(F("[rfid] receiver: gain ")); Serial.print(g); Serial.print(F(", level ")); Serial.print(l);
      Serial.println(F(" (until the next upload)"));
    } else Serial.println(F("[rfid] use rx <gain 0-7> <level 0-15>, e.g. rx 0 12"));
  } else if (cmd == "help") {
    help();
  } else {
    Serial.print(F("[rfid] unknown command: ")); Serial.println(cmd);
    help();
  }
}

/* ---------------------------------------------------------------- fast card detection */

// Looking for a card uses WUPA, not REQA: WUPA also reaches a card that is halted or was left
// half-read by a failed attempt. This clone RC522 reports a garbled answer on almost every poll,
// even with no card near it, and those garbled frames can leave a real card half-read. With REQA
// such a card stayed silent until the reader restarted (every 5 s), so taps were seconds late.
// Now any poll that isn't a clean answer restarts the field, so an arriving card always starts clean.

// 0 = no answer, 1 = garbled answer (this reader's normal noise), 2 = a card answered
byte cardInField() {
  reader.PCD_WriteRegister(MFRC522::TxModeReg, 0x00);
  reader.PCD_WriteRegister(MFRC522::RxModeReg, 0x00);
  reader.PCD_WriteRegister(MFRC522::ModWidthReg, 0x26);
  byte atqa[2];
  byte size = sizeof(atqa);
  MFRC522::StatusCode st = reader.PICC_WakeupA(atqa, &size);
  if (st == MFRC522::STATUS_OK || st == MFRC522::STATUS_COLLISION) return 2;
  return st == MFRC522::STATUS_TIMEOUT ? 0 : 1;
}

// Switch the reader's field off and on: every card in it starts over, ready for a clean read.
void restartField() {
  reader.PCD_AntennaOff();
  delay(5);
  reader.PCD_AntennaOn();
  delay(5);                             // a card needs a few ms of field before it can answer
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
    readerSetup();
  }

  byte inField = cardInField();
  if (inField == 0) return;
  if (inField == 1 || !reader.PICC_ReadCardSerial()) { restartField(); return; }   // try again cleanly

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
