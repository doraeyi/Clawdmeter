#include "sysinfo.h"
#include <Arduino.h>
#include "../lv_mem_psram.h"

#if defined(ESP_PLATFORM)
#include <esp_ota_ops.h>
#include <esp_chip_info.h>
#include <nvs.h>
#include <esp_heap_caps.h>

void sysinfo_read(SysInfo* s) {
    s->chip        = ESP.getChipModel();
    s->chip_rev    = ESP.getChipRevision();
    s->cores       = ESP.getChipCores();
    s->cpu_mhz     = ESP.getCpuFreqMHz();
    s->flash_total = ESP.getFlashChipSize();
    const esp_partition_t* run = esp_ota_get_running_partition();
    s->app_part    = run ? run->size : 0;
    s->app_used    = ESP.getSketchSize();
    s->psram_total = ESP.getPsramSize();
    s->psram_free  = ESP.getFreePsram();
    s->heap_total  = ESP.getHeapSize();
    s->heap_free   = ESP.getFreeHeap();
    s->heap_min_free = ESP.getMinFreeHeap();
    s->heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s->lvgl_used = lv_mem_psram_used();
    nvs_stats_t st;
    if (nvs_get_stats(NULL, &st) == ESP_OK) {
        s->nvs_used = st.used_entries;
        s->nvs_total = st.total_entries;
    } else {
        s->nvs_used = s->nvs_total = 0;
    }
    s->chip_temp_c = temperatureRead();
    s->uptime_s    = millis() / 1000;
}

#else  // desktop simulator

void sysinfo_read(SysInfo* s) {
    s->chip = "Simulator"; s->chip_rev = 0; s->cores = 2; s->cpu_mhz = 240;
    s->flash_total = 16u << 20;
    s->app_part = 6553600; s->app_used = 5069195;
    s->psram_total = 8u << 20; s->psram_free = 6900000;
    s->heap_total = 327680; s->heap_free = 182000; s->heap_min_free = 150000;
    s->heap_largest = 110000;
    s->lvgl_used = lv_mem_psram_used();
    s->nvs_used = 42; s->nvs_total = 630;
    s->chip_temp_c = 41.5f;
    s->uptime_s = millis() / 1000;
}

#endif
