#pragma once
// On-demand Wi-Fi for the board (secrets in net/secrets.h, see secrets.example.h).
//
// Wi-Fi is OFF almost all the time. It is switched on only:
//   - for a few seconds after boot, to set the clock from the internet (NTP);
//   - while the Smart Plug app is open and the computer isn't relaying, to
//     talk to the Tapo plugs directly; it goes off again ~60 s after the app
//     is closed.
// All network work runs on a background task; these calls never block.
#include <stdbool.h>
#include <stdint.h>

#define NET_MAX_PLUGS 4

enum NetState {
    NET_OFF,          // Wi-Fi off / idle
    NET_CONNECTING,   // joining Wi-Fi / talking to plugs, no result yet
    NET_READY,        // plug results available
    NET_ERR_WIFI,     // couldn't join the Wi-Fi network
    NET_ERR_AUTH,     // Tapo rejected the account / password
};

struct NetPlug {
    char name[48];
    bool on;
    bool ok;        // reachable
};

bool net_configured(void);          // secrets.h present with Wi-Fi + Tapo settings
bool net_time_configured(void);     // secrets.h present with Wi-Fi settings
void net_init(void);                // starts the worker; kicks off the boot-time clock sync

// Clock from NTP. Returns true once (per sync) with the local wall-clock epoch.
bool net_take_time(long* local_epoch, int* fmt);

// Smart Plug direct mode.
void net_plugs_open(void);          // app opened: Wi-Fi on, read all plugs
void net_plugs_close(void);         // app closed: Wi-Fi off after a grace period
void net_plugs_refresh(void);       // re-read all plug states
void net_plug_set(int i, bool on);  // switch one plug (state re-read afterwards)
// Copies the latest results; returns the plug count. *gen changes on every update.
int  net_plugs_snapshot(NetPlug* out, int max, NetState* state, uint32_t* gen);
