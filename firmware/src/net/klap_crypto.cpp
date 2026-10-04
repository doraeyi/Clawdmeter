#include "klap_crypto.h"
#include <string.h>

#if defined(ESP_PLATFORM)
#include <mbedtls/sha256.h>
#include <mbedtls/sha1.h>
#include <mbedtls/md5.h>
#include <mbedtls/aes.h>
#include <esp_random.h>

struct Sha256 {
    mbedtls_sha256_context c;
    Sha256()  { mbedtls_sha256_init(&c); mbedtls_sha256_starts(&c, 0); }
    ~Sha256() { mbedtls_sha256_free(&c); }
    void add(const void* p, size_t n) { mbedtls_sha256_update(&c, (const uint8_t*)p, n); }
    void done(uint8_t out[32]) { mbedtls_sha256_finish(&c, out); }
};
static void sha1(const void* p, size_t n, uint8_t out[20]) { mbedtls_sha1((const uint8_t*)p, n, out); }
static void md5(const void* p, size_t n, uint8_t out[16])  { mbedtls_md5((const uint8_t*)p, n, out); }
static bool aes_cbc(bool enc, const uint8_t key[16], const uint8_t iv_in[16], const uint8_t* in, size_t n, uint8_t* out) {
    mbedtls_aes_context a;
    mbedtls_aes_init(&a);
    uint8_t iv[16];
    memcpy(iv, iv_in, 16);
    int r = enc ? mbedtls_aes_setkey_enc(&a, key, 128) : mbedtls_aes_setkey_dec(&a, key, 128);
    if (r == 0) r = mbedtls_aes_crypt_cbc(&a, enc ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, n, iv, in, out);
    mbedtls_aes_free(&a);
    return r == 0;
}
void klap_random(uint8_t* out, size_t len) { esp_fill_random(out, len); }

#else  // host test build (OpenSSL)
#include <openssl/evp.h>
#include <openssl/rand.h>
struct Sha256 {
    EVP_MD_CTX* c;
    Sha256()  { c = EVP_MD_CTX_new(); EVP_DigestInit_ex(c, EVP_sha256(), nullptr); }
    ~Sha256() { EVP_MD_CTX_free(c); }
    void add(const void* p, size_t n) { EVP_DigestUpdate(c, p, n); }
    void done(uint8_t out[32]) { unsigned l; EVP_DigestFinal_ex(c, out, &l); }
};
static void sha1(const void* p, size_t n, uint8_t out[20]) { unsigned l; EVP_Digest(p, n, out, &l, EVP_sha1(), nullptr); }
static void md5(const void* p, size_t n, uint8_t out[16])  { unsigned l; EVP_Digest(p, n, out, &l, EVP_md5(), nullptr); }
static bool aes_cbc(bool enc, const uint8_t key[16], const uint8_t iv[16], const uint8_t* in, size_t n, uint8_t* out) {
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    int l1 = 0, l2 = 0;
    bool ok = EVP_CipherInit_ex(c, EVP_aes_128_cbc(), nullptr, key, iv, enc ? 1 : 0) == 1;
    EVP_CIPHER_CTX_set_padding(c, 0);
    ok = ok && EVP_CipherUpdate(c, out, &l1, in, (int)n) == 1 && EVP_CipherFinal_ex(c, out + l1, &l2) == 1;
    EVP_CIPHER_CTX_free(c);
    return ok;
}
void klap_random(uint8_t* out, size_t len) { RAND_bytes(out, (int)len); }
#endif

void klap_auth_hash_v2(const char* user, const char* pass, uint8_t out[32]) {
    uint8_t u[20], p[20];
    sha1(user, strlen(user), u);
    sha1(pass, strlen(pass), p);
    Sha256 h; h.add(u, 20); h.add(p, 20); h.done(out);
}

void klap_auth_hash_v1(const char* user, const char* pass, uint8_t out[16]) {
    uint8_t up[32];
    md5(user, strlen(user), up);
    md5(pass, strlen(pass), up + 16);
    md5(up, 32, out);
}

void klap_hs1_expected(const uint8_t local[16], const uint8_t remote[16],
                       const uint8_t* auth, size_t auth_len, uint8_t out[32]) {
    Sha256 h;
    h.add(local, 16);
    if (auth_len == 32) h.add(remote, 16);
    h.add(auth, auth_len);
    h.done(out);
}

void klap_hs2_payload(const uint8_t local[16], const uint8_t remote[16],
                      const uint8_t* auth, size_t auth_len, uint8_t out[32]) {
    Sha256 h;
    h.add(remote, 16);
    if (auth_len == 32) h.add(local, 16);
    h.add(auth, auth_len);
    h.done(out);
}

static void derive(const char* label, const uint8_t l[16], const uint8_t r[16],
                   const uint8_t* a, size_t al, uint8_t out[32]) {
    Sha256 h; h.add(label, strlen(label)); h.add(l, 16); h.add(r, 16); h.add(a, al); h.done(out);
}

void klap_session_init(KlapSession* s, const uint8_t local[16], const uint8_t remote[16],
                       const uint8_t* auth, size_t auth_len) {
    uint8_t d[32];
    derive("lsk", local, remote, auth, auth_len, d);
    memcpy(s->key, d, 16);
    derive("iv", local, remote, auth, auth_len, d);
    memcpy(s->iv12, d, 12);
    s->seq = (int32_t)(((uint32_t)d[28] << 24) | ((uint32_t)d[29] << 16) | ((uint32_t)d[30] << 8) | d[31]);
    derive("ldk", local, remote, auth, auth_len, d);
    memcpy(s->sig, d, 28);
}

static void put_be32(uint8_t* p, int32_t v) {
    uint32_t u = (uint32_t)v;
    p[0] = u >> 24; p[1] = u >> 16; p[2] = u >> 8; p[3] = u;
}

size_t klap_encrypt(KlapSession* s, const uint8_t* msg, size_t len, uint8_t* out, int32_t* seq_out) {
    s->seq = (int32_t)((uint32_t)s->seq + 1u);
    uint8_t iv[16];
    memcpy(iv, s->iv12, 12);
    put_be32(iv + 12, s->seq);

    const size_t padded = (len / 16 + 1) * 16;
    uint8_t* ct = out + 32;
    memcpy(ct, msg, len);                                   // PKCS7 pad in place, then encrypt
    memset(ct + len, (int)(padded - len), padded - len);
    if (!aes_cbc(true, s->key, iv, ct, padded, ct)) return 0;

    uint8_t seqb[4];
    put_be32(seqb, s->seq);
    Sha256 h; h.add(s->sig, 28); h.add(seqb, 4); h.add(ct, padded); h.done(out);
    *seq_out = s->seq;
    return 32 + padded;
}

int klap_decrypt(const KlapSession* s, int32_t seq, const uint8_t* in, size_t len, uint8_t* out) {
    if (len < 48 || (len - 32) % 16) return -1;
    uint8_t iv[16];
    memcpy(iv, s->iv12, 12);
    put_be32(iv + 12, seq);
    const size_t n = len - 32;
    if (!aes_cbc(false, s->key, iv, in + 32, n, out)) return -1;
    const uint8_t pad = out[n - 1];
    if (pad == 0 || pad > 16) return -1;
    out[n - pad] = 0;
    return (int)(n - pad);
}
