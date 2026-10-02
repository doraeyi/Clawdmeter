#pragma once
// Launcher / app manager.
//
//   HOME (clock) ──swipe left/right──▶ LAUNCHER (app grid) ──tap──▶ APP
//        ▲                                  │                         │
//        └──────── swipe up from the bottom bar (anywhere) ◀──────────┘
//
// Extra gestures: horizontal swipe on the launcher goes back home; a swipe
// right from the left edge inside an app goes back to the launcher.
#include <stdint.h>
#include <stdbool.h>
#include "../data.h"

enum app_view_t { VIEW_HOME, VIEW_LAUNCHER, VIEW_APP };

void app_manager_init(void);          // after ui_init(); shows HOME
void app_manager_tick(void);          // every loop

// Fed from the touch read callback with the already-filtered press state.
// Returns true when a navigation gesture fired — the caller should then make
// LVGL ignore the rest of this press (lv_indev_wait_release) so the widget
// under the finger doesn't also get a click.
bool app_manager_touch(bool pressed, int x, int y);

// PWR short press. Routed to the front app first; default = brightness cycle.
void app_manager_on_pwr(void);

// Every valid daemon payload (used for the home-screen clock).
void app_manager_on_usage(const UsageData* d);

void       app_manager_go_home(void);
void       app_manager_open_launcher(void);
void       app_manager_open_app(int index);
app_view_t app_manager_view(void);
