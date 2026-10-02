#include "app_registry.h"

// Each app lives in its own file and exports one `const App`.
extern const App APP_CLAUDE;
extern const App APP_WEATHER;
extern const App APP_SMART_PLUG;
extern const App APP_NOW_PLAYING;

// Launcher order. To add an app: declare it above and add it here.
const App* const APPS[] = {
    &APP_CLAUDE,
    &APP_WEATHER,
    &APP_SMART_PLUG,
    &APP_NOW_PLAYING,
};
const int APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);
