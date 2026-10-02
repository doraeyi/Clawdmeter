// Weak "not available" defaults for hal_extras.h. A board overrides any of
// these by defining the same function (strong symbol) in its own folder.
#include "hal_extras.h"

__attribute__((weak)) bool power_hal_details(PowerDetails*)       { return false; }
__attribute__((weak)) bool imu_hal_accel(float*, float*, float*)  { return false; }
__attribute__((weak)) void imu_hal_app_mode(bool)                 {}
__attribute__((weak)) bool sound_hal_mic_start(int*)              { return false; }
__attribute__((weak)) int  sound_hal_mic_latest(int16_t*, int)    { return 0; }
__attribute__((weak)) void sound_hal_mic_stop(void)               {}
