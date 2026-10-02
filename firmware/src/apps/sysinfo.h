#pragma once
#include <stdint.h>

// Chip / memory / storage figures for the Settings app.
struct SysInfo {
    const char* chip;          // e.g. "ESP32-S3"
    int         chip_rev;
    int         cores;
    int         cpu_mhz;
    uint32_t    flash_total;   // bytes, whole flash chip
    uint32_t    app_part;      // bytes, running app partition
    uint32_t    app_used;      // bytes, current firmware image
    uint32_t    psram_total, psram_free;
    uint32_t    heap_total, heap_free, heap_min_free;
    uint32_t    heap_largest;  // largest free internal block (what Wi-Fi needs)
    uint32_t    lvgl_used;     // bytes LVGL has allocated (in PSRAM on S3)
    uint32_t    nvs_used, nvs_total;   // NVS entries (settings storage)
    float       chip_temp_c;   // internal sensor, -999 if unavailable
    uint32_t    uptime_s;
};
void sysinfo_read(SysInfo* out);
