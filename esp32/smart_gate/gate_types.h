// Data types for the gate sketch. They live in a header so the Arduino IDE
// knows them before it generates the sketch's function prototypes.
#pragma once
#include <Arduino.h>

// One HC-SR04, read without blocking: an interrupt times the echo pulse.
struct Sonar {
  uint8_t trig = 0, echo = 0;
  volatile uint32_t riseUs = 0, fallUs = 0;
  volatile bool rose = false, fell = false;
  uint32_t pingAtUs = 0;
  bool waiting = false;
  uint8_t silentPings = 0;      // pings in a row where the sensor never answered
  float last[3] = {NAN, NAN, NAN};
  uint8_t idx = 0;
  float cm = NAN;               // filtered distance; NAN = nothing in range
  bool fault = false;           // sensor not answering (unplugged, no power, broken wire)
};

// "Is something there?" with a confirm count and hysteresis, so noise cannot flicker it.
struct Detector {
  bool on = false;
  uint8_t inCount = 0, outCount = 0;
};

enum GateState : uint8_t { G_CLOSED, G_OPENING, G_OPEN, G_CLOSING, G_STOPPED };
enum Mode : uint8_t { M_AUTO, M_MANUAL };
enum Action : uint8_t { A_OPEN, A_CLOSE, A_AUTO, A_MANUAL, A_ESTOP, A_RESET };

// A command from the website, passed from the network task to the gate logic.
struct Command {
  char id[40];
  Action action;
};

// An allowed RFID card (from Firebase /gate/cards, cached in flash).
struct CardEntry { char uid[21]; char name[33]; };

// The last card tapped, waiting to be reported to the website.
struct CardEvent { char uid[21]; char name[33]; char result[12]; uint32_t seq; };

// Everything the website shows, copied between the two cores under a lock.
struct Snapshot {
  float cm; bool sensorFault; bool vehicle;
  float clearCm; bool clearFault; bool laneBusy;
  uint8_t state; uint8_t mode; int angle; bool estop;
  char note[96];
  char lastCmd[40]; char lastCmdResult[72];
};
