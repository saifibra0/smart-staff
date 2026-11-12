// Smart Parking automatic entrance gate: settings you can change.
// Re-upload the sketch after editing this file.
#pragma once

// ---------------------------------------------------------------- pins (ESP32 DevKit V1, 30 or 38 pin)
// These are all ordinary GPIOs: none is a boot strapping pin or input-only.
#define PIN_TRIG          26   // HC-SR04 TRIG (3.3 V output is enough to trigger it)
#define PIN_ECHO          27   // HC-SR04 ECHO, through the 1 kΩ / 2 kΩ voltage divider (5 V -> 3.3 V)
#define PIN_SERVO         18   // servo signal (orange/yellow wire)
#define PIN_STATUS_LED     2   // on-board blue LED on most DevKit boards

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
#define DETECT_CONFIRM            3  // readings in a row needed to say "detected" (filters noise)
#define RELEASE_CONFIRM           5  // readings in a row needed to say "gone"
#define CLEAR_DETECT_CM       20.0f  // clearance sensor: something closer than this is in the lane
#define CLEAR_HOLD_MS          2000  // lane must stay clear this long before closing by itself

// ---------------------------------------------------------------- HC-SR04 timing
#define PING_INTERVAL_MS         70  // one reading every 70 ms (the sensor needs at least 60 ms)
#define ECHO_TIMEOUT_US       30000  // no echo end after 30 ms (~5 m) = nothing in range
#define SENSOR_FAULT_PINGS       10  // this many pings with no echo at all = sensor fault
#define MAX_RANGE_CM          400.0f

// ---------------------------------------------------------------- servo
#define SERVO_CLOSED_DEG          0  // barrier down
#define SERVO_OPEN_DEG           90  // barrier up
#define SERVO_STEP_MS            15  // ms per degree: 90 degrees takes about 1.4 s
#define SERVO_MIN_US            500  // pulse width at 0 degrees (SG90 / MG996R: 500-2500)
#define SERVO_MAX_US           2500  // pulse width at 180 degrees

// ---------------------------------------------------------------- network
#define CMD_POLL_MS             400  // how often the ESP32 checks for a new command from the website
#define STATE_MIN_GAP_MS        250  // fastest it sends status updates while something changes
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
