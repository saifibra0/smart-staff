// Smart Parking automatic entrance gate: settings you can change.
// Re-upload the sketch after editing this file.
#pragma once

// ---------------------------------------------------------------- pins (ESP32 DevKit V1, 30 or 38 pin)
// These are all ordinary GPIOs: none is a boot strapping pin or input-only.
#define PIN_TRIG          26   // HC-SR04 TRIG (3.3 V output is enough to trigger it)
#define PIN_ECHO          27   // HC-SR04 ECHO, through the 1 kΩ / 2 kΩ voltage divider (5 V -> 3.3 V)
#define PIN_SERVO         18   // servo signal (orange/yellow wire)
#define PIN_STATUS_LED     2   // on-board blue LED on most DevKit boards

// Gate traffic light: two LEDs, each from its pin through a 220-330 ohm resistor to the LED's long leg,
// short leg to GND. Red while the barrier is down (or closing), green while it is up (or opening),
// red blinking fast during an emergency stop. GPIO 6-11 can never be used: they belong to the flash chip.
#define GATE_LIGHTS_ENABLED 1
#define PIN_LIGHT_GREEN   21   // board label D21
#define PIN_LIGHT_RED     22   // board label D22

// Optional second HC-SR04 that looks across the lane just AFTER the barrier.
// Only with this sensor will the gate close by itself. Set to 1 when it is wired.
#define CLEAR_SENSOR_ENABLED 0
#define PIN_CLEAR_TRIG    32
#define PIN_CLEAR_ECHO    33   // through its own 1 kΩ / 2 kΩ divider

// Optional push button to GND for a local emergency stop (uses the internal pull-up).
#define ESTOP_BUTTON_ENABLED 0
#define PIN_ESTOP_BUTTON  25

// ---------------------------------------------------------------- vehicle detection
#define DETECT_MIN_CM          5.0f  // detection zone: a vehicle between DETECT_MIN_CM and DETECT_CM
#define DETECT_CM             10.0f  //   opens the gate (closer than 5 cm or farther than 10 cm does not)
#define RELEASE_HYSTERESIS_CM  5.0f  // it must move past DETECT_CM + this to count as gone
#define DETECT_CONFIRM            2  // readings in a row needed to say "detected" (filters noise)
#define RELEASE_CONFIRM           5  // readings in a row needed to say "gone"
#define CLEAR_DETECT_CM       20.0f  // clearance sensor: something closer than this is in the lane
#define CLEAR_HOLD_MS          2000  // lane must stay clear this long before closing by itself
// With only the entrance sensor (AUTOMATIC mode): close by itself this long after the vehicle has left
// the detection zone, giving it time to drive under the barrier. 0 = never close by itself (only the
// CLOSE GATE button). Not used when the clearance sensor is installed.
#define AUTO_CLOSE_AFTER_MS    3000

// ---------------------------------------------------------------- HC-SR04 timing
#define PING_INTERVAL_MS         60  // one reading every 60 ms (the sensor needs at least 60 ms)
#define ECHO_TIMEOUT_US       30000  // no echo end after 30 ms (~5 m) = nothing in range
#define SENSOR_FAULT_PINGS       10  // this many pings with no echo at all = sensor fault
#define MAX_RANGE_CM          400.0f

// ---------------------------------------------------------------- servo
#define SERVO_CLOSED_DEG          0  // barrier down
#define SERVO_OPEN_DEG           90  // barrier up
#define SERVO_STEP_MS             8  // ms per degree: 90 degrees takes about 0.7 s (15 = 1.4 s, smoother)
#define SERVO_MIN_US            500  // pulse width at 0 degrees (SG90 / MG996R: 500-2500)
#define SERVO_MAX_US           2500  // pulse width at 180 degrees

// ---------------------------------------------------------------- network
#define CMD_POLL_MS             400  // backup only: how often to check for commands if the live command stream is down
#define STATE_MIN_GAP_MS        100  // fastest it sends status updates while something changes
#define HEARTBEAT_MS           2000  // status update at least this often, so the website knows it is online
#define CMD_MAX_AGE_MS        15000  // ignore website commands older than this (e.g. sent while offline)
#define HTTP_TIMEOUT_MS        5000

// ---------------------------------------------------------------- local network (same Wi-Fi as your laptop)
#define DEVICE_HOSTNAME     "smart-gate"  // the board answers as smart-gate.local
#define STATUS_PAGE_ENABLED 1             // view-only status page at http://smart-gate.local
#define OTA_ENABLED         1             // firmware uploads over Wi-Fi (also needs OTA_PASSWORD in secrets.h)

// ---------------------------------------------------------------- Wi-Fi setup from a laptop or phone
#define SETUP_AP_NAME       "SmartGate-Setup"  // the ESP32's own Wi-Fi, open while it has no working Wi-Fi
#define SETUP_AP_PASSWORD   "smartgate"        // needed to join it (8+ characters); change it for your own build
#define SETUP_AFTER_MS      30000              // open the setup Wi-Fi if the saved Wi-Fi fails for this long

// ---------------------------------------------------------------- RFID card reader (RC522, 3.3 V)
// RC522 -> ESP32: SDA -> GPIO 5, SCK -> GPIO 14, MOSI -> GPIO 23, MISO -> GPIO 19, RST -> GPIO 4,
//                 3.3V -> 3V3, GND -> GND, IRQ not connected.
#define RFID_ENABLED        1
#define PIN_RFID_SS         5
#define PIN_RFID_SCK       14
#define PIN_RFID_MOSI      23
#define PIN_RFID_MISO      19
#define PIN_RFID_RST        4
#define RFID_GAIN        0x00   // receiver gain: 0x00 lowest ... 0x70 highest. Some RC522 clones only read at the lowest
#define RFID_POLL_MS      150   // how often to look for a card
#define CARDS_SYNC_MS   60000   // re-read the allowed cards from the website (the website also asks right after a change)
#define MAX_CARDS          50
