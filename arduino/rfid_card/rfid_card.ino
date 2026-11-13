/*
 * Smart Parking: RFID card setup (Arduino UNO + RC522 reader)
 * ------------------------------------------------------------
 * Registers NFC/RFID cards (13.56 MHz: MIFARE Classic, MIFARE Ultralight, NTAG...) as allowed
 * cards and checks them when they are tapped. Allowed cards are kept in the UNO's EEPROM, so
 * they survive power-off and new uploads.
 *
 * Library: MFRC522 by GithubCommunity (Arduino IDE: Tools > Manage Libraries > "MFRC522").
 * Board:   Arduino UNO (Tools > Board > Arduino AVR Boards > Arduino Uno).
 *
 * Wiring (RC522 -> UNO):
 *   SDA  -> D10      SCK  -> D13      MOSI -> D11      MISO -> D12
 *   RST  -> D9       GND  -> GND      3.3V -> 3.3V (NOT 5V)      IRQ -> not connected
 * Optional: green LED (with 220 ohm) on D7, red LED on D6, buzzer on D5.
 *
 * Use it from the Serial Monitor at 115200 baud (line ending: Newline):
 *   add     the next card you tap is registered as allowed
 *   remove  the next card you tap is removed
 *   list    shows the registered cards
 *   clear   removes all cards
 *   help    shows these commands
 * Tapping a card otherwise prints ACCESS GRANTED or ACCESS DENIED with the card's UID.
 */
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>

const uint8_t PIN_SS = 10;        // RC522 SDA (chip select)
const uint8_t PIN_RST = 9;        // RC522 RST
const uint8_t PIN_GREEN = 7;      // optional
const uint8_t PIN_RED = 6;        // optional
const uint8_t PIN_BUZZER = 5;     // optional

const uint8_t MAX_CARDS = 20;
const uint8_t UID_MAX = 10;       // RC522 UIDs are 4, 7 or 10 bytes
const uint8_t EE_MAGIC = 0xC4;    // marks the EEPROM as ours
const int EE_COUNT = 1, EE_CARDS = 2, EE_SLOT = 1 + UID_MAX;   // slot = length byte + UID bytes
const unsigned long WAIT_MS = 15000;                            // how long add/remove waits for a tap
// Receiver gain. This RC522 (chip version 0x82) garbles card replies at medium or high gain and only
// reads reliably at the lowest gain. If cards are not detected, run "test" and pick the gain that answers.
const byte READER_GAIN = MFRC522::RxGain_min;

MFRC522 reader(PIN_SS, PIN_RST);

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

/* ---------------------------------------------------------------- feedback */

void signal(bool ok) {
  digitalWrite(ok ? PIN_GREEN : PIN_RED, HIGH);
  if (ok) tone(PIN_BUZZER, 2000, 120); else tone(PIN_BUZZER, 400, 400);
  delay(ok ? 400 : 600);
  digitalWrite(PIN_GREEN, LOW);
  digitalWrite(PIN_RED, LOW);
}

void help() {
  Serial.println(F("[rfid] commands: add | remove | list | clear | help  (Serial Monitor at 115200, line ending: Newline)"));
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
  SPI.begin();
  reader.PCD_Init();
  delay(50);
  reader.PCD_SetAntennaGain(READER_GAIN);
  Serial.println();
  Serial.println(F("Smart Parking RFID card setup (UNO + RC522)"));

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
  }

  reader.PICC_HaltA();
  reader.PCD_StopCrypto1();
}
