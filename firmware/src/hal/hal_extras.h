#pragma once
#include <stdint.h>
#include <stdbool.h>

// Optional board extras used by the launcher apps (Settings, Sensors, Tilt,
// Spectrum). Every function has a weak default in hal_extras_default.cpp that
// reports "not available", so a board only implements what it actually has —
// no #ifdef BOARD_* in shared code and no edits needed in other ports.

// ---- Power (PMU details) ----
struct PowerDetails {
    int   pct;            // 0..100, -1 unknown
    int   batt_mv;        // battery voltage, 0 if unknown / no battery
    int   vbus_mv;        // USB input voltage, 0 if unknown
    int   sys_mv;         // system rail voltage, 0 if unknown
    float pmu_temp_c;     // PMU die temperature, NAN-ish (-999) if unknown
    bool  charging;
    bool  vbus_in;
    bool  batt_present;
};
bool power_hal_details(PowerDetails* out);   // false = not supported

// ---- IMU (accelerometer) ----
// Latest acceleration in g, in the sensor's own axes. false = no IMU.
bool imu_hal_accel(float* x, float* y, float* z);
// App mode: freeze auto-rotation and sample faster (tilt game, level).
void imu_hal_app_mode(bool on);

// ---- Microphone ----
// Start capturing; returns false if the board has no mic path. *sample_rate
// receives the capture rate in Hz.
bool sound_hal_mic_start(int* sample_rate);
// Copy the newest `n` mono samples (int16) into `out`; returns how many were
// copied (0 if nothing captured yet).
int  sound_hal_mic_latest(int16_t* out, int n);
void sound_hal_mic_stop(void);
