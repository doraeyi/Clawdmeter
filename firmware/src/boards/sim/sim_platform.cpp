#include "sim_platform.h"
#include <SDL.h>
#include <Arduino.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool quit = false;

static bool     pwr_down = false;
static uint32_t pwr_down_ms = 0;
static bool     pwr_long_fired = false;
static bool     edge_pressed = false, edge_long = false, edge_released = false;

static int  battery = 87;
static bool charging = false;

static bool take(bool* f) { bool v = *f; *f = false; return v; }
bool sim_take_pwr_pressed(void)  { return take(&edge_pressed); }
bool sim_take_pwr_long(void)     { return take(&edge_long); }
bool sim_take_pwr_released(void) { return take(&edge_released); }
int  sim_battery_pct(void) { return battery; }
bool sim_charging(void)    { return charging; }
bool sim_should_quit(void) { return quit; }

// Matches the AXP2101 long-press threshold main.cpp's pair gesture expects.
#define PWR_LONG_MS 1500

// ---- Scripted input (SIM_INPUT) ----
struct ScriptStep { uint32_t ms; char op[8]; int a, b; char path[160]; };
static ScriptStep* script = nullptr;
static int  script_n = 0, script_i = 0;
static bool script_loaded = false;
static bool s_touch = false;
static int  s_x = 0, s_y = 0;

static void script_load(void) {
    script_loaded = true;
    const char* f = getenv("SIM_INPUT");
    if (!f) return;
    FILE* fp = fopen(f, "r");
    if (!fp) { printf("SIM_INPUT: cannot open %s\n", f); return; }
    script = (ScriptStep*)calloc(512, sizeof(ScriptStep));
    char line[256];
    while (script_n < 512 && fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        ScriptStep& st = script[script_n];
        unsigned long ms = 0;
        int n = sscanf(line, "%lu %7s", &ms, st.op);
        if (n < 2) continue;
        st.ms = (uint32_t)ms;
        if (!strcmp(st.op, "shot")) sscanf(line, "%*lu %*s %159s", st.path);
        else sscanf(line, "%*lu %*s %d %d", &st.a, &st.b);
        script_n++;
    }
    fclose(fp);
    printf("SIM_INPUT: %d steps\n", script_n);
}

static void script_run(void) {
    if (!script_loaded) script_load();
    while (script_i < script_n && millis() >= script[script_i].ms) {
        ScriptStep& st = script[script_i++];
        if      (!strcmp(st.op, "down")) { s_touch = true;  s_x = st.a; s_y = st.b; }
        else if (!strcmp(st.op, "move")) { s_x = st.a; s_y = st.b; }
        else if (!strcmp(st.op, "up"))   { s_touch = false; }
        else if (!strcmp(st.op, "pwr"))  { edge_pressed = true; edge_released = true; }
        else if (!strcmp(st.op, "shot")) { sim_display_screenshot(st.path); }
        else if (!strcmp(st.op, "quit")) { quit = true; }
    }
}

bool sim_script_touch(uint16_t* x, uint16_t* y, bool* pressed) {
    if (!script) return false;
    *x = (uint16_t)s_x; *y = (uint16_t)s_y; *pressed = s_touch;
    return true;
}

void sim_pump(void) {
    script_run();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit = true;
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            SDL_Keycode k = e.key.keysym.sym;
            switch (k) {
            case SDLK_ESCAPE: quit = true; break;
            case SDLK_SPACE:  sim_playback_toggle(); break;
            case SDLK_LEFT:   sim_playback_step(-1); break;
            case SDLK_RIGHT:  sim_playback_step(+1); break;
            case SDLK_d:      sim_playback_toggle_link(); break;
            case SDLK_s:      sim_display_screenshot(NULL); break;
            case SDLK_c:      charging = !charging; break;
            case SDLK_MINUS:  battery = battery < 5 ? 0 : battery - 5; break;
            case SDLK_EQUALS: battery = battery > 95 ? 100 : battery + 5; break;
            case SDLK_p:
                pwr_down = true;
                pwr_down_ms = millis();
                pwr_long_fired = false;
                break;
            default:
                if (k >= SDLK_1 && k <= SDLK_9) sim_playback_jump(k - SDLK_1);
                break;
            }
        }
        if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_p && pwr_down) {
            pwr_down = false;
            if (!pwr_long_fired) edge_pressed = true;
            edge_released = true;
        }
    }
    if (pwr_down && !pwr_long_fired && millis() - pwr_down_ms >= PWR_LONG_MS) {
        pwr_long_fired = true;
        edge_long = true;
    }

    // Headless CI hook: SIM_AUTOSHOT_MS=<ms> → screenshot + exit.
    static long autoshot_ms = -2;
    if (autoshot_ms == -2) {
        const char* v = getenv("SIM_AUTOSHOT_MS");
        autoshot_ms = v ? atol(v) : -1;
    }
    if (autoshot_ms >= 0 && millis() >= (uint32_t)autoshot_ms) {
        const char* p = getenv("SIM_AUTOSHOT_PATH");
        sim_display_screenshot(p ? p : "sim-autoshot.bmp");
        quit = true;
        autoshot_ms = -1;
    }
}
