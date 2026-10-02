#include "app_registry.h"

// Each app lives in its own file and exports one `const App`.
extern const App APP_CLAUDE;
extern const App APP_WEATHER;
extern const App APP_SMART_PLUG;
extern const App APP_NOW_PLAYING;
extern const App APP_SENSORS;
extern const App APP_TILT;
extern const App APP_SPECTRUM;
extern const App APP_SETTINGS;

// Launcher order. To add an app: declare it above and add it here.
const App* const APPS[] = {
    &APP_CLAUDE,
    &APP_NOW_PLAYING,
    &APP_WEATHER,
    &APP_SMART_PLUG,
    &APP_SENSORS,
    &APP_TILT,
    &APP_SPECTRUM,
    &APP_SETTINGS,
};
const int APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);
