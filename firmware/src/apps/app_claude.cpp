// Claude app — wraps the original Clawdmeter screens (usage + splash
// animation) from ui.cpp / splash.cpp without changing how they work.
//   open        → usage view (session / weekly bars)
//   PWR button  → usage: show the Clawd animation; animation: next animation
//   tap screen  → toggles animation ↔ usage (original behaviour)
#include "app.h"
#include "../ui.h"
#include "../splash.h"
#include "../clawd_still.h"

static lv_image_dsc_t icon_dsc;

static void claude_enter(void) { ui_show_screen(SCREEN_USAGE); }
static void claude_leave(void) { ui_show_screen(SCREEN_NONE); }

static bool claude_on_pwr(void) {
    if (ui_get_current_screen() == SCREEN_SPLASH) splash_next();
    else                                          ui_show_screen(SCREEN_SPLASH);
    return true;
}

static const lv_image_dsc_t* claude_icon(void) {
    icon_dsc.header.w      = CLAWD_STILL_W;
    icon_dsc.header.h      = CLAWD_STILL_H;
    icon_dsc.header.cf     = LV_COLOR_FORMAT_RGB565A8;
    icon_dsc.header.stride = CLAWD_STILL_W * 2;
    icon_dsc.data          = clawd_still_data;
    icon_dsc.data_size     = CLAWD_STILL_W * CLAWD_STILL_H * 3;
    return &icon_dsc;
}

// The icon descriptor must be filled before the launcher reads it; a static
// initializer object does that before setup() runs.
static struct ClaudeIconInit { ClaudeIconInit() { claude_icon(); } } s_icon_init;

extern const App APP_CLAUDE = {
    /*name*/       "Claude",
    /*icon_glyph*/ nullptr,
    /*icon_img*/   &icon_dsc,
    /*color*/      0xd97757,
    /*create*/     nullptr,        // draws with ui.cpp's existing screens
    /*enter*/      claude_enter,
    /*leave*/      claude_leave,
    /*tick*/       nullptr,        // ui_tick_anim()/splash_tick() already run in loop()
    /*on_pwr*/     claude_on_pwr,
    /*label*/      "Claude",
};
