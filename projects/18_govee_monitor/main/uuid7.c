#include "uuid7.h"

#include <string.h>
#include <stdio.h>

#define FNV_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL
/* Golden-ratio constant, used only to decorrelate the second hash. */
#define SEED_B    0x9E3779B97F4A7C15ULL

static uint64_t fnv1a64(const uint8_t *p, size_t len, uint64_t seed)
{
    uint64_t h = seed;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

void uuid7_deterministic(int64_t epoch_ms, const char *device_id,
                         const uint8_t mac[6], char *out)
{
    /* Canonical hash input: device, sensor MAC, timestamp. Any change to this
     * layout changes every id ever minted, so it is effectively frozen. */
    uint8_t buf[64];
    size_t  n = 0;

    size_t dlen = device_id ? strlen(device_id) : 0;
    if (dlen > 40) dlen = 40;
    if (dlen) memcpy(buf, device_id, dlen);
    n += dlen;

    for (int i = 0; i < 6; i++) buf[n++] = mac ? mac[i] : 0;
    for (int i = 7; i >= 0; i--) {
        buf[n++] = (uint8_t)((uint64_t)epoch_ms >> (i * 8));
    }

    uint64_t h1 = fnv1a64(buf, n, FNV_BASIS);
    uint64_t h2 = fnv1a64(buf, n, FNV_BASIS ^ SEED_B);

    uint8_t b[16];
    /* 48-bit big-endian millisecond timestamp — the time-ordered prefix. */
    for (int i = 0; i < 6; i++) {
        b[i] = (uint8_t)((uint64_t)epoch_ms >> ((5 - i) * 8));
    }
    b[6] = (uint8_t)(0x70 | ((h1 >> 8) & 0x0F));   /* version 7 + rand_a hi */
    b[7] = (uint8_t)(h1 & 0xFF);                   /* rand_a lo */
    b[8] = (uint8_t)(0x80 | ((h2 >> 56) & 0x3F));  /* variant 0b10 + rand_b */
    for (int i = 9; i < 16; i++) {
        b[i] = (uint8_t)(h2 >> ((15 - i) * 8));
    }

    static const char hex[] = "0123456789abcdef";
    int o = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[o++] = '-';
        out[o++] = hex[b[i] >> 4];
        out[o++] = hex[b[i] & 0x0F];
    }
    out[o] = '\0';
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int64_t uuid7_extract_ms(const char *uuid)
{
    int64_t ms = 0;
    int     got = 0;
    for (const char *p = uuid; *p && got < 12; p++) {
        int v = hexval(*p);
        if (v < 0) continue;              /* skip hyphens */
        ms = (ms << 4) | v;
        got++;
    }
    return got == 12 ? ms : -1;
}
