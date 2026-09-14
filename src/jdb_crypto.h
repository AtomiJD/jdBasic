// jdb_crypto.h - the operating system's secure random source and
// PBKDF2-HMAC-SHA256, shared by the interpreter (vm.cpp) and the native
// runtime (jdb_runtime.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
extern "C" long __stdcall BCryptGenRandom(void* algorithm, unsigned char* buffer,
                                         unsigned long count, unsigned long flags);
#pragma comment(lib, "bcrypt.lib")
#elif defined(__linux__)
#include <sys/random.h>
#include <cerrno>
#elif defined(__APPLE__) || defined(__EMSCRIPTEN__)
#include <unistd.h>
#include <sys/random.h>
#endif

namespace jdb_crypto {

inline constexpr int64_t RANDOM_BYTES_MAX = 1 << 20;
inline constexpr int64_t PBKDF2_BYTES_MAX = 1 << 16;

// Fills out with n bytes from the OS generator; false when there is none.
inline bool random_bytes(uint8_t* out, size_t n) {
#if defined(_WIN32)
    const unsigned long SYSTEM_PREFERRED_RNG = 0x00000002;
    while (n > 0) {
        unsigned long chunk = n > 0x40000000 ? 0x40000000UL : (unsigned long)n;
        if (BCryptGenRandom(nullptr, out, chunk, SYSTEM_PREFERRED_RNG) != 0) return false;
        out += chunk;
        n -= chunk;
    }
    return true;
#elif defined(__linux__)
    while (n > 0) {
        ssize_t got = getrandom(out, n, 0);
        if (got < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        out += got;
        n -= (size_t)got;
    }
    return true;
#elif defined(__APPLE__) || defined(__EMSCRIPTEN__)
    while (n > 0) {
        size_t chunk = n > 256 ? 256 : n;
        if (getentropy(out, chunk) != 0) return false;
        out += chunk;
        n -= chunk;
    }
    return true;
#else
    (void)out;
    return n == 0;
#endif
}

struct Sha256 {
    uint32_t h[8];
    uint8_t block[64];
    size_t used;
    uint64_t total;
};

inline uint32_t sha256_rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

inline void sha256_compress(uint32_t h[8], const uint8_t* p) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotr(w[i - 15], 7) ^ sha256_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = sha256_rotr(w[i - 2], 17) ^ sha256_rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = s1 + w[i - 7] + s0 + w[i - 16];
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + (sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25)) +
                      ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22)) +
                      ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

inline void sha256_init(Sha256& s) {
    static const uint32_t H0[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                   0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s.h, H0, sizeof(H0));
    s.used = 0;
    s.total = 0;
}

inline void sha256_update(Sha256& s, const uint8_t* data, size_t len) {
    s.total += len;
    while (len > 0) {
        size_t take = 64 - s.used;
        if (take > len) take = len;
        memcpy(s.block + s.used, data, take);
        s.used += take;
        data += take;
        len -= take;
        if (s.used == 64) {
            sha256_compress(s.h, s.block);
            s.used = 0;
        }
    }
}

inline void sha256_final(Sha256& s, uint8_t out[32]) {
    uint64_t bits = s.total * 8;
    uint8_t pad = 0x80;
    sha256_update(s, &pad, 1);
    uint8_t zero = 0;
    while (s.used != 56) sha256_update(s, &zero, 1);
    uint8_t len_be[8];
    for (int i = 0; i < 8; i++) len_be[i] = (uint8_t)(bits >> (56 - i * 8));
    sha256_update(s, len_be, 8);
    for (int i = 0; i < 8; i++) {
        out[i * 4]     = (uint8_t)(s.h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s.h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s.h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(s.h[i]);
    }
}

// PBKDF2 per RFC 8018 with HMAC-SHA256 as the PRF. The inner and outer
// key blocks are hashed once up front, so each iteration costs two
// compressions instead of four.
inline void pbkdf2_hmac_sha256(const uint8_t* password, size_t password_len,
                               const uint8_t* salt, size_t salt_len,
                               uint64_t iterations, uint8_t* out, size_t out_len) {
    uint8_t key[64] = {0};
    if (password_len > 64) {
        Sha256 k;
        sha256_init(k);
        sha256_update(k, password, password_len);
        sha256_final(k, key);
    } else if (password_len) {
        memcpy(key, password, password_len);
    }
    uint8_t pad[64];
    Sha256 inner0, outer0;
    for (int i = 0; i < 64; i++) pad[i] = key[i] ^ 0x36;
    sha256_init(inner0);
    sha256_update(inner0, pad, 64);
    for (int i = 0; i < 64; i++) pad[i] = key[i] ^ 0x5c;
    sha256_init(outer0);
    sha256_update(outer0, pad, 64);

    uint32_t block_index = 1;
    while (out_len > 0) {
        uint8_t counter[4] = {(uint8_t)(block_index >> 24), (uint8_t)(block_index >> 16),
                              (uint8_t)(block_index >> 8), (uint8_t)block_index};
        uint8_t u[32], t[32];
        Sha256 s = inner0;
        sha256_update(s, salt, salt_len);
        sha256_update(s, counter, 4);
        sha256_final(s, u);
        s = outer0;
        sha256_update(s, u, 32);
        sha256_final(s, u);
        memcpy(t, u, 32);
        for (uint64_t it = 1; it < iterations; it++) {
            s = inner0;
            sha256_update(s, u, 32);
            sha256_final(s, u);
            s = outer0;
            sha256_update(s, u, 32);
            sha256_final(s, u);
            for (int i = 0; i < 32; i++) t[i] ^= u[i];
        }
        size_t take = out_len < 32 ? out_len : 32;
        memcpy(out, t, take);
        out += take;
        out_len -= take;
        block_index++;
    }
}

// A version 4 UUID from 16 random bytes, into a 37-byte buffer.
inline void uuid4_format(uint8_t b[16], char out[37]) {
    static const char* hex = "0123456789abcdef";
    b[6] = (uint8_t)((b[6] & 0x0F) | 0x40);
    b[8] = (uint8_t)((b[8] & 0x3F) | 0x80);
    size_t pos = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[pos++] = '-';
        out[pos++] = hex[b[i] >> 4];
        out[pos++] = hex[b[i] & 0x0F];
    }
    out[pos] = 0;
}

}  // namespace jdb_crypto
