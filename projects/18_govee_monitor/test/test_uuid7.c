#include "uuid7.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); fails++; } \
} while (0)

#define DEV "device 1"

static const uint8_t MAC_A[6] = {0xA4,0xC1,0x38,0xDE,0x38,0x9B};
static const uint8_t MAC_B[6] = {0xA4,0xC1,0x38,0x1C,0xED,0x5C};
/* differs from MAC_A only in the last byte — the hash must still separate them */
static const uint8_t MAC_NEAR[6] = {0xA4,0xC1,0x38,0xDE,0x38,0x9C};

/* Layout must be conformant even though the entropy is not random. */
static void test_rfc_layout(void)
{
    char u[UUID7_STR_LEN];
    uuid7_deterministic(1787693134000LL, DEV, MAC_A, u);

    CHECK(strlen(u) == 36, "length %zu", strlen(u));
    CHECK(u[8] == '-' && u[13] == '-' && u[18] == '-' && u[23] == '-', "hyphens: %s", u);
    CHECK(u[14] == '7', "version nibble is '%c', want '7' (%s)", u[14], u);
    /* variant 0b10xx → first hex of that group is 8, 9, a or b */
    CHECK(u[19] == '8' || u[19] == '9' || u[19] == 'a' || u[19] == 'b',
          "variant nibble '%c' (%s)", u[19], u);
}

/* The property the whole retry story rests on. */
static void test_determinism(void)
{
    char a[UUID7_STR_LEN], b[UUID7_STR_LEN];
    uuid7_deterministic(1787693134000LL, DEV, MAC_A, a);
    uuid7_deterministic(1787693134000LL, DEV, MAC_A, b);
    CHECK(strcmp(a, b) == 0, "same inputs must give same id:\n  %s\n  %s", a, b);
}

/* Every axis must actually change the id, or rows would collide. */
static void test_inputs_separate(void)
{
    char base[UUID7_STR_LEN], other[UUID7_STR_LEN];
    uuid7_deterministic(1787693134000LL, DEV, MAC_A, base);

    uuid7_deterministic(1787693134000LL, DEV, MAC_B, other);
    CHECK(strcmp(base, other) != 0, "sensor MAC must change the id");

    uuid7_deterministic(1787693134000LL, DEV, MAC_NEAR, other);
    CHECK(strcmp(base, other) != 0, "one-byte MAC difference must change the id");

    uuid7_deterministic(1787693134000LL, "device 2", MAC_A, other);
    CHECK(strcmp(base, other) != 0, "device must change the id");

    uuid7_deterministic(1787693134001LL, DEV, MAC_A, other);
    CHECK(strcmp(base, other) != 0, "timestamp must change the id");
}

/* v7's whole point: lexicographic order == chronological order. */
static void test_sorts_by_time(void)
{
    char prev[UUID7_STR_LEN] = {0}, cur[UUID7_STR_LEN];
    int64_t t0 = 1787693134000LL;
    for (int i = 0; i < 500; i++) {
        int64_t t = t0 + (int64_t)i * 180000;      /* 3-minute buckets */
        uuid7_deterministic(t, DEV, MAC_A, cur);
        if (i) CHECK(strcmp(prev, cur) < 0,
                     "id %d not greater than its predecessor:\n  %s\n  %s", i, prev, cur);
        memcpy(prev, cur, sizeof(cur));
    }
}

static void test_timestamp_roundtrip(void)
{
    int64_t times[] = { 0, 1LL, 1787693134000LL, 1893456000000LL };
    for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); i++) {
        char u[UUID7_STR_LEN];
        uuid7_deterministic(times[i], DEV, MAC_B, u);
        int64_t back = uuid7_extract_ms(u);
        CHECK(back == times[i], "roundtrip %lld -> %lld (%s)",
              (long long)times[i], (long long)back, u);
    }
}

/* Two boards must never mint the same id for the same sensor and instant. */
static void test_no_cross_device_collision(void)
{
    char a[UUID7_STR_LEN], b[UUID7_STR_LEN];
    const uint8_t *MACS[4] = { MAC_A, MAC_B, MAC_NEAR, MAC_A };
    for (int s = 0; s < 4; s++) {
        for (int i = 0; i < 200; i++) {
            int64_t t = 1787693134000LL + (int64_t)i * 180000;
            uuid7_deterministic(t, "device 1", MACS[s], a);
            uuid7_deterministic(t, "device 2", MACS[s], b);
            CHECK(strcmp(a, b) != 0, "collision across devices at s=%d i=%d", s, i);
        }
    }
}

int main(void)
{
    test_rfc_layout();
    test_determinism();
    test_inputs_separate();
    test_sorts_by_time();
    test_timestamp_roundtrip();
    test_no_cross_device_collision();

    if (fails == 0) { printf("all uuid7 tests passed\n"); return 0; }
    printf("%d check(s) failed\n", fails);
    return 1;
}
