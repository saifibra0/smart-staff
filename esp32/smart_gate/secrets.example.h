// Copy this file to secrets.h (same folder) and fill it in.
// secrets.h is listed in .gitignore, so your passwords never go to GitHub.
#pragma once

// Your Wi-Fi (2.4 GHz; the ESP32 cannot join 5 GHz networks).
#define WIFI_SSID       "your-wifi-name"
#define WIFI_PASSWORD   "your-wifi-password"

// From the Firebase console: Project settings > General > Web API key.
// It is public (it is also in data.js); access is controlled by the database rules.
#define FIREBASE_API_KEY  "AIzaSyCRhKAvNMRPp4kJle02YklmN8P8uXeQK0Y"

// Realtime Database URL, without a trailing slash. Shown at the top of
// Firebase console > Build > Realtime Database > Data.
#define FIREBASE_DB_URL   "https://smart-staff-2de6a-default-rtdb.europe-west1.firebasedatabase.app"

// The gate's own Firebase account. Create it in Firebase console >
// Build > Authentication > Users > Add user. Use it only for this ESP32.
// (smart_gate.ino currently sets DEVICE_EMAIL itself, to gate-esp32-2@smart-staff.iot.)
#define DEVICE_EMAIL      "gate-esp32-2@smart-staff.iot"
#define DEVICE_PASSWORD   "choose-a-long-random-password"

// Password for firmware uploads over Wi-Fi, at least 8 characters, not used anywhere else.
// Arduino IDE asks for it when you upload to the network port "smart-gate".
// Leave it as it is to keep Wi-Fi uploads off.
#define OTA_PASSWORD      "choose-an-update-password"
