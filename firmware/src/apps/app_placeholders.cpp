// Placeholder apps — each gets its own launcher tile and page now, and will be
// moved into its own app_<name>.cpp when it's actually implemented.
#include "app.h"

extern const App APP_WEATHER;
extern const App APP_SMART_PLUG;

static void weather_create(lv_obj_t* root)     { app_make_placeholder(root, &APP_WEATHER,     "Coming soon"); }
static void plug_create(lv_obj_t* root)        { app_make_placeholder(root, &APP_SMART_PLUG,  "Coming soon"); }

extern const App APP_WEATHER = {
    "Weather", ICON_CLOUD_SUN, nullptr, 0x6ab0de,
    weather_create, nullptr, nullptr, nullptr, nullptr,
};

extern const App APP_SMART_PLUG = {
    "Smart Plug", ICON_PLUG, nullptr, 0x788c5d,
    plug_create, nullptr, nullptr, nullptr, nullptr,
};
