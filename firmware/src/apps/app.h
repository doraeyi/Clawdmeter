#pragma once
// ---------------------------------------------------------------------------
// App interface for the launcher.
//
// Every app is one self-contained .cpp file in this folder that defines a
// single `const App` instance. To add an app:
//   1. copy app_template.cpp.txt → app_<name>.cpp and fill in the callbacks
//   2. add one line to the APPS[] list in app_registry.cpp
// Nothing else changes, so adding an app never touches the existing ones.
//
// Lifecycle (driven by app_manager):
//   create(root) — once at boot. `root` is a full-screen, initially hidden
//                  container owned by the manager; build your widgets in it.
//                  May be NULL for apps that draw elsewhere (e.g. Claude).
//   enter()      — the app was opened from the launcher (root already shown).
//   leave()      — the user went home / back (root about to be hidden).
//   tick()       — called every loop while the app is in front.
//   on_pwr()     — PWR (middle) button short press while the app is in front.
//                  Return true if handled; false falls back to the system
//                  default (cycle screen brightness).
// Any callback may be NULL.
// ---------------------------------------------------------------------------
#include <lvgl.h>
#include <stdint.h>

struct App {
    const char*            name;        // shown under the launcher icon / as page title
    const char*            icon_glyph;  // UTF-8 glyph in font_icons_64 (NULL → use icon_img)
    const lv_image_dsc_t*  icon_img;    // optional image icon (used when icon_glyph is NULL)
    uint32_t               color;       // accent color (0xRRGGBB) for the icon

    void (*create)(lv_obj_t* root);
    void (*enter)(void);
    void (*leave)(void);
    void (*tick)(void);
    bool (*on_pwr)(void);

    const char*            label;       // optional display name (UTF-8, CJK ok); NULL → name
};

// Font Awesome 6 (solid) glyphs compiled into font_icons_64.
// Regenerate font_icons_64.c with lv_font_conv to add more (see docs/apps.md).
#define ICON_CLOUD_SUN  "\xEF\x9B\x84"   // U+F6C4
#define ICON_PLUG       "\xEF\x87\xA6"   // U+F1E6
#define ICON_MUSIC      "\xEF\x80\x81"   // U+F001
#define ICON_CLOUD      "\xEF\x83\x82"   // U+F0C2
#define ICON_SUN        "\xEF\x86\x85"   // U+F185
#define ICON_PREV       "\xEF\x81\x88"   // U+F048 backward-step
#define ICON_NEXT       "\xEF\x81\x91"   // U+F051 forward-step
#define ICON_PLAY       "\xEF\x81\x8B"   // U+F04B
#define ICON_PAUSE      "\xEF\x81\x8C"   // U+F04C
#define ICON_GEAR       "\xEF\x80\x93"   // U+F013 gear (Settings)
#define ICON_WAVE       "\xEF\xA0\xBE"   // U+F83E wave-square (Spectrum)

// Display name for a launcher tile / page title.
inline const char* app_label(const struct App* a) { return a->label ? a->label : a->name; }

// Helpers shared by simple apps (implemented in app_manager.cpp).
// Builds the standard page header (app name, centered at the top) and returns
// the label so the app can restyle it if it wants.
lv_obj_t* app_make_title(lv_obj_t* root, const char* text);
// Standard "not built yet" body: big icon + one dim line of text.
void      app_make_placeholder(lv_obj_t* root, const App* app, const char* line);
