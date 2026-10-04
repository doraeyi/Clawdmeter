#pragma once
// TP-Link Tapo plug (P100/P105/P110/P115) over the local KLAP protocol.
// Blocking — call from a worker task, never from the LVGL loop.
#include <stddef.h>
#include <stdint.h>
#include "klap_crypto.h"

enum TapoResult { TAPO_OK = 0, TAPO_ERR_NET = -1, TAPO_ERR_AUTH = -2, TAPO_ERR_DEVICE = -3 };

struct TapoAuth {
    uint8_t v2[32];
    uint8_t v1[16];
    void set(const char* user, const char* pass) {
        klap_auth_hash_v2(user, pass, v2);
        klap_auth_hash_v1(user, pass, v1);
    }
};

class TapoClient {
public:
    void init(const char* host, const TapoAuth* auth);
    int  get_info(bool* on, char* nickname, size_t nick_len);   // nickname may be NULL
    int  set_on(bool on);
    void reset() { ready_ = false; }

private:
    int handshake();
    int request(const char* json, char* plain, size_t plain_max);

    char            host_[40] = "";
    const TapoAuth* auth_ = nullptr;
    bool            ready_ = false;
    KlapSession     s_;
    char            cookie_[96] = "";
    uint32_t        hs_ms_ = 0;
};

// Decodes standard base64 into out (NUL-terminated). Returns length or -1.
int tapo_b64_decode(const char* in, char* out, size_t out_max);
