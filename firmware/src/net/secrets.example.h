// Copy this file to secrets.h (same folder) and fill it in. secrets.h is in
// .gitignore — it stays on your computer and is compiled into the firmware.
// Leave secrets.h out entirely to build without Wi-Fi.
#pragma once

// Home Wi-Fi (2.4 GHz — the ESP32 can't use 5 GHz).
#define WIFI_SSID       "your-wifi-name"
#define WIFI_PASSWORD   "your-wifi-password"

// Tapo app (TP-Link ID) login. In the Tapo app also turn on
// Me → Third-Party Services → Third-Party Compatibility.
#define TAPO_USERNAME   "you@example.com"
#define TAPO_PASSWORD   "your-tapo-password"

// Plug IP addresses (Tapo app → plug → ⚙ → Device Info). Reserve them in
// your router so they don't change. Up to 4.
#define TAPO_PLUG_IPS   { "192.168.1.50", "192.168.1.51", "192.168.1.52" }
// Optional names, same order. Remove this line to use the names from the Tapo app.
#define TAPO_PLUG_NAMES { "檯燈", "電風扇", "除濕機" }

// Clock. POSIX time zone: Taiwan = "CST-8". 24 or 12 hour.
#define TIME_ZONE       "CST-8"
#define TIME_FORMAT     24
