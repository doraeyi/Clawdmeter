#include "http_post.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <Arduino.h>
#include <WiFiClient.h>

struct Conn {
    WiFiClient c;
    uint32_t deadline;
    bool open(const char* host, uint16_t port, uint32_t timeout_ms) {
        deadline = millis() + timeout_ms;
        c.setTimeout(timeout_ms);
        return c.connect(host, port, (int32_t)timeout_ms);
    }
    bool write(const void* p, size_t n) { return c.write((const uint8_t*)p, n) == n; }
    int  read_byte() {   // -1 on timeout / closed
        while ((int32_t)(deadline - millis()) > 0) {
            if (c.available()) return c.read();
            if (!c.connected()) return -1;
            delay(1);
        }
        return -1;
    }
    void close() { c.stop(); }
};

#else
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>

struct Conn {
    int fd = -1;
    std::chrono::steady_clock::time_point deadline;
    bool open(const char* host, uint16_t port, uint32_t timeout_ms) {
        deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char ps[8];
        snprintf(ps, sizeof(ps), "%u", port);
        if (getaddrinfo(host, ps, &hints, &res) != 0) return false;
        fd = socket(res->ai_family, res->ai_socktype, 0);
        bool ok = fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen) == 0;
        freeaddrinfo(res);
        return ok;
    }
    bool write(const void* p, size_t n) { return send(fd, p, n, 0) == (ssize_t)n; }
    int read_byte() {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) return -1;
        pollfd pf{fd, POLLIN, 0};
        if (poll(&pf, 1, (int)left) <= 0) return -1;
        unsigned char b;
        return recv(fd, &b, 1, 0) == 1 ? b : -1;
    }
    void close() { if (fd >= 0) ::close(fd); fd = -1; }
};
#endif

// Reads one header line (without CRLF). Returns false on EOF/timeout.
static bool read_line(Conn& c, char* buf, size_t max) {
    size_t n = 0;
    for (;;) {
        int b = c.read_byte();
        if (b < 0) return false;
        if (b == '\n') break;
        if (b != '\r' && n + 1 < max) buf[n++] = (char)b;
    }
    buf[n] = 0;
    return true;
}

static bool starts_with_ci(const char* s, const char* prefix) {
    for (; *prefix; s++, prefix++)
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
    return true;
}

HttpResult http_post(const char* host, uint16_t port, const char* path, const char* cookie,
                     const uint8_t* body, size_t len, uint8_t* resp, size_t resp_max,
                     uint32_t timeout_ms) {
    HttpResult r{};
    r.status = -1;
    Conn c;
    if (!c.open(host, port, timeout_ms)) { c.close(); return r; }

    char hdr[384];
    int hl = snprintf(hdr, sizeof(hdr),
                      "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/octet-stream\r\n"
                      "Content-Length: %u\r\nConnection: close\r\n%s%s%s\r\n",
                      path, host, (unsigned)len,
                      cookie ? "Cookie: " : "", cookie ? cookie : "", cookie ? "\r\n" : "");
    if (hl <= 0 || hl >= (int)sizeof(hdr) || !c.write(hdr, hl) || (len && !c.write(body, len))) {
        c.close();
        return r;
    }

    char line[256];
    if (!read_line(c, line, sizeof(line)) || sscanf(line, "HTTP/%*s %d", &r.status) != 1) {
        r.status = -1;
        c.close();
        return r;
    }
    long content_len = -1;
    while (read_line(c, line, sizeof(line)) && line[0]) {
        if (starts_with_ci(line, "content-length:")) content_len = atol(line + 15);
        else if (starts_with_ci(line, "set-cookie:") && !r.set_cookie[0]) {
            const char* v = line + 11;
            while (*v == ' ') v++;
            size_t n = strcspn(v, ";");
            if (n >= sizeof(r.set_cookie)) n = sizeof(r.set_cookie) - 1;
            memcpy(r.set_cookie, v, n);
            r.set_cookie[n] = 0;
        }
    }
    size_t got = 0;
    while (content_len < 0 || (long)got < content_len) {
        int b = c.read_byte();
        if (b < 0) break;
        if (got < resp_max) resp[got] = (uint8_t)b;
        got++;
    }
    c.close();
    if (content_len >= 0 && (long)got < content_len) { r.status = -1; return r; }   // truncated
    r.body_len = got > resp_max ? resp_max : got;
    if (got > resp_max) r.status = -1;
    return r;
}
