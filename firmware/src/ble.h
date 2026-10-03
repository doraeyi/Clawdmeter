#pragma once
#include <stdint.h>

enum ble_state_t {
    BLE_STATE_INIT,
    BLE_STATE_ADVERTISING,
    BLE_STATE_CONNECTED,
    BLE_STATE_DISCONNECTED,
};

void ble_init(void);
void ble_tick(void);
ble_state_t ble_get_state(void);
const char* ble_get_device_name(void);
const char* ble_get_mac_address(void);
void ble_clear_bonds(void);
bool ble_has_bonds(void);
bool ble_has_data(void);
const char* ble_get_data(void);
void ble_send_ack(void);
void ble_send_nack(void);
void ble_request_refresh(void);

void ble_set_battery_level(int pct);

// BLE HID keyboard
void ble_keyboard_press(uint8_t key, uint8_t modifier);
void ble_keyboard_release(void);

// Now-playing payload from the host (separate characteristic from usage data)
bool ble_has_now_playing(void);
const char* ble_get_now_playing(void);

// BLE HID consumer control (media keys): press + release in one call.
#define MEDIA_PLAY_PAUSE 0x00CD
#define MEDIA_NEXT       0x00B5
#define MEDIA_PREV       0x00B6
#define MEDIA_VOL_UP     0x00E9
#define MEDIA_VOL_DOWN   0x00EA
#define MEDIA_MUTE       0x00E2
void ble_media_key(uint16_t usage);

// Smart plugs: state JSON written by the host; commands notified back to it.
bool ble_has_plugs(void);
const char* ble_get_plugs(void);
bool ble_plug_command(const char* json);   // false if not connected
