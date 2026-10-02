#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Bytes currently allocated by LVGL (widgets, styles, strings, draw layers).
size_t lv_mem_psram_used(void);
#ifdef __cplusplus
}
#endif
