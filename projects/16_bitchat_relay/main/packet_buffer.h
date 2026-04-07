#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bloom filter for packet deduplication */
#define BLOOM_BITS       8192   /* 1KB bit array */
#define BLOOM_HASH_COUNT    3
#define BLOOM_TTL_SEC     300   /* 5 minutes, matches BitChat spec */

/* Ring buffer for store-and-forward */
#define PACKET_MAX_SIZE   2048  /* BitChat max padded size */
#define RING_CAPACITY       32  /* Max buffered packets (32×2KB = 64KB) */

typedef struct {
    uint8_t  data[PACKET_MAX_SIZE];
    uint16_t len;
    uint8_t  ttl;
    int8_t   rssi;
    uint32_t timestamp_ms;
    uint8_t  source_peer[8];   /* Who we got it from — don't relay back */
} buffered_packet_t;

/**
 * Initialize bloom filter and ring buffer.
 */
void packet_buffer_init(void);

/**
 * Check if packet was seen before (bloom filter).
 * Returns true if PROBABLY seen (duplicate), false if DEFINITELY new.
 * If new, automatically marks it as seen.
 */
bool packet_buffer_is_duplicate(const uint8_t *data, uint16_t len);

/**
 * Add a packet to the store-and-forward ring buffer.
 * Oldest packet is evicted if full.
 * Returns true on success.
 */
bool packet_buffer_push(const uint8_t *data, uint16_t len, uint8_t ttl,
                        int8_t rssi, const uint8_t *source_peer);

/**
 * Pop the oldest buffered packet for relay.
 * Returns true if a packet was available, fills out.
 */
bool packet_buffer_pop(buffered_packet_t *out);

/**
 * Get current buffer count.
 */
uint32_t packet_buffer_count(void);

/**
 * Get bloom filter fill percentage (0.0 - 100.0).
 */
float packet_buffer_bloom_fill_pct(void);

/**
 * Periodically clear expired bloom entries (call from timer).
 */
void packet_buffer_bloom_maintain(void);

#ifdef __cplusplus
}
#endif
