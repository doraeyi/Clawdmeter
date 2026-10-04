#pragma once
// TP-Link KLAP (Tapo P100/P105/P110 local API) crypto — pure functions, no I/O.
// Mirrors python-kasa's KlapTransportV2 / KlapEncryptionSession.
//
//   auth_hash  v2 = sha256(sha1(user) + sha1(pass))          (32 bytes)
//              v1 = md5(md5(user) + md5(pass))                (16 bytes)
//   handshake1: POST local_seed(16) → remote_seed(16) + server_hash(32)
//               server_hash v2 = sha256(local + remote + auth), v1 = sha256(local + auth)
//   handshake2: POST v2 sha256(remote + local + auth), v1 sha256(remote + auth)
//   session:   key = sha256("lsk"+L+R+A)[:16], iv = sha256("iv"+L+R+A): [:12] + seq(BE int32 = [28:32])
//              sig = sha256("ldk"+L+R+A)[:28]
//   request:   seq++, AES-128-CBC(key, iv12+seq) PKCS7; body = sha256(sig+seq+ct) + ct; URL ?seq=<seq>
#include <stddef.h>
#include <stdint.h>

struct KlapSession {
    uint8_t key[16];
    uint8_t iv12[12];
    uint8_t sig[28];
    int32_t seq;
};

void klap_auth_hash_v2(const char* user, const char* pass, uint8_t out[32]);
void klap_auth_hash_v1(const char* user, const char* pass, uint8_t out[16]);

// Expected handshake1 server hash / handshake2 payload for a given auth hash
// (auth_len 32 → v2 formulas, 16 → v1).
void klap_hs1_expected(const uint8_t local[16], const uint8_t remote[16],
                       const uint8_t* auth, size_t auth_len, uint8_t out[32]);
void klap_hs2_payload(const uint8_t local[16], const uint8_t remote[16],
                      const uint8_t* auth, size_t auth_len, uint8_t out[32]);

void klap_session_init(KlapSession* s, const uint8_t local[16], const uint8_t remote[16],
                       const uint8_t* auth, size_t auth_len);

// Bytes needed for klap_encrypt's output.
static inline size_t klap_encrypted_size(size_t len) { return 32 + (len / 16 + 1) * 16; }

// Encrypts `msg`, increments the session seq; returns body length, *seq_out = seq for the URL.
size_t klap_encrypt(KlapSession* s, const uint8_t* msg, size_t len, uint8_t* out, int32_t* seq_out);

// Decrypts a response body (32-byte signature + ciphertext) for `seq`.
// Writes plaintext (+ NUL) into out (needs len - 32 + 1 bytes). Returns plaintext length or -1.
int klap_decrypt(const KlapSession* s, int32_t seq, const uint8_t* in, size_t len, uint8_t* out);

void klap_random(uint8_t* out, size_t len);
