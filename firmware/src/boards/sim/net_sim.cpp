// Simulator stand-in for net/ (no Wi-Fi). SIM_NET=1 fakes three plugs reached
// directly over Wi-Fi so the Smart Plug app's direct mode can be screenshotted.
#include "../../net/net.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static NetPlug  plugs[3] = {{"檯燈", true, true}, {"電風扇", false, true}, {"除濕機", false, false}};
static uint32_t opened_ms = 0, gen = 1;
static bool     open_ = false;

bool net_configured(void)      { return getenv("SIM_NET") != nullptr; }
bool net_time_configured(void) { return net_configured(); }
void net_init(void) {}
bool net_take_time(long*, int*) { return false; }
void net_plugs_open(void)  { open_ = true; opened_ms = millis(); gen++; }
void net_plugs_close(void) { open_ = false; }
void net_plugs_refresh(void) {}
void net_plug_set(int i, bool on) {
    printf("[sim] net plug %d -> %d\n", i, on);
    if (i >= 0 && i < 3 && plugs[i].ok) { plugs[i].on = on; gen++; }
}
int net_plugs_snapshot(NetPlug* out, int max, NetState* state, uint32_t* g) {
    const bool ready = open_ && millis() - opened_ms > 1500;
    if (state) *state = !open_ ? NET_OFF : ready ? NET_READY : NET_CONNECTING;
    if (g) *g = gen + (ready ? 1000 : 0);
    if (!ready) return 0;
    int n = max < 3 ? max : 3;
    memcpy(out, plugs, sizeof(NetPlug) * n);
    return n;
}
