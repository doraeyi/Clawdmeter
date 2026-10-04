#pragma once
// Minimal blocking HTTP/1.1 POST (binary body, Connection: close).
// Implemented with WiFiClient on the ESP32 and POSIX sockets in host tests.
#include <stddef.h>
#include <stdint.h>

struct HttpResult {
    int    status;          // HTTP status, or < 0 on connect / I/O error
    size_t body_len;
    char   set_cookie[96];  // first Set-Cookie value (up to the first ';'), or ""
};

HttpResult http_post(const char* host, uint16_t port, const char* path, const char* cookie,
                     const uint8_t* body, size_t len, uint8_t* resp, size_t resp_max,
                     uint32_t timeout_ms);
