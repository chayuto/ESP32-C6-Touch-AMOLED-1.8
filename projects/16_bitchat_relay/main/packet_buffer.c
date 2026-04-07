/*
 * packet_buffer.c — Bloom filter deduplication + ring buffer for opaque relay
 *
 * Bloom filter: 8192-bit (1KB) array with 3 hash functions.
 * Periodically cleared to handle TTL expiry (full reset every BLOOM_TTL_SEC).
 *
 * Ring buffer: fixed-capacity FIFO for store-and-forward relay.
 * Oldest packets evicted when full.
 */

#include "packet_buffer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "pkt_buf";

/* ── Bloom Filter ────────────────────────────────────────── */

static uint8_t s_bloom[BLOOM_BITS / 8];
static uint32_t s_bloom_set_count;
static int64_t  s_bloom_last_clear;
static SemaphoreHandle_t s_bloom_mutex;

/* FNV-1a hash with seed variation for multiple hash functions */
static uint32_t fnv1a_hash(const uint8_t *data, uint16_t len, uint32_t seed)
{
    uint32_t hash = 2166136261u ^ seed;
    for (uint16_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

static void bloom_set(uint32_t bit_idx)
{
    s_bloom[bit_idx / 8] |= (1u << (bit_idx % 8));
}

static bool bloom_test(uint32_t bit_idx)
{
    return (s_bloom[bit_idx / 8] & (1u << (bit_idx % 8))) != 0;
}

/* ── Ring Buffer ─────────────────────────────────────────── */

static buffered_packet_t s_ring[RING_CAPACITY];
static uint32_t s_ring_head;  /* Next write position */
static uint32_t s_ring_tail;  /* Next read position */
static uint32_t s_ring_count;
static SemaphoreHandle_t s_ring_mutex;

/* ── Public API ──────────────────────────────────────────── */

void packet_buffer_init(void)
{
    memset(s_bloom, 0, sizeof(s_bloom));
    s_bloom_set_count = 0;
    s_bloom_last_clear = esp_timer_get_time();
    s_bloom_mutex = xSemaphoreCreateMutex();

    memset(s_ring, 0, sizeof(s_ring));
    s_ring_head = 0;
    s_ring_tail = 0;
    s_ring_count = 0;
    s_ring_mutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "Bloom: %u bits (%u bytes), %d hashes, %ds TTL",
             BLOOM_BITS, (unsigned)(sizeof(s_bloom)), BLOOM_HASH_COUNT, BLOOM_TTL_SEC);
    ESP_LOGI(TAG, "Ring: %d slots x %d bytes max = %uKB",
             RING_CAPACITY, PACKET_MAX_SIZE,
             (unsigned)(sizeof(s_ring) / 1024));
}

bool packet_buffer_is_duplicate(const uint8_t *data, uint16_t len)
{
    xSemaphoreTake(s_bloom_mutex, portMAX_DELAY);

    bool all_set = true;
    uint32_t indices[BLOOM_HASH_COUNT];

    for (int i = 0; i < BLOOM_HASH_COUNT; i++) {
        uint32_t h = fnv1a_hash(data, len, (uint32_t)i * 0x9E3779B9u);
        indices[i] = h % BLOOM_BITS;
        if (!bloom_test(indices[i])) {
            all_set = false;
        }
    }

    if (all_set) {
        xSemaphoreGive(s_bloom_mutex);
        return true;  /* Probably seen before */
    }

    /* Novel — mark as seen */
    for (int i = 0; i < BLOOM_HASH_COUNT; i++) {
        bloom_set(indices[i]);
    }
    s_bloom_set_count += BLOOM_HASH_COUNT;

    xSemaphoreGive(s_bloom_mutex);
    return false;  /* Definitely new */
}

bool packet_buffer_push(const uint8_t *data, uint16_t len, uint8_t ttl,
                        int8_t rssi, const uint8_t *source_peer)
{
    if (len > PACKET_MAX_SIZE) {
        ESP_LOGW(TAG, "Packet too large: %u > %d", len, PACKET_MAX_SIZE);
        return false;
    }

    xSemaphoreTake(s_ring_mutex, portMAX_DELAY);

    buffered_packet_t *slot = &s_ring[s_ring_head];
    memcpy(slot->data, data, len);
    slot->len = len;
    slot->ttl = ttl;
    slot->rssi = rssi;
    slot->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (source_peer) {
        memcpy(slot->source_peer, source_peer, 8);
    } else {
        memset(slot->source_peer, 0, 8);
    }

    s_ring_head = (s_ring_head + 1) % RING_CAPACITY;
    if (s_ring_count < RING_CAPACITY) {
        s_ring_count++;
    } else {
        /* Overwrite oldest — advance tail */
        s_ring_tail = (s_ring_tail + 1) % RING_CAPACITY;
    }

    xSemaphoreGive(s_ring_mutex);
    return true;
}

bool packet_buffer_pop(buffered_packet_t *out)
{
    xSemaphoreTake(s_ring_mutex, portMAX_DELAY);

    if (s_ring_count == 0) {
        xSemaphoreGive(s_ring_mutex);
        return false;
    }

    memcpy(out, &s_ring[s_ring_tail], sizeof(buffered_packet_t));
    s_ring_tail = (s_ring_tail + 1) % RING_CAPACITY;
    s_ring_count--;

    xSemaphoreGive(s_ring_mutex);
    return true;
}

uint32_t packet_buffer_count(void)
{
    return s_ring_count;
}

float packet_buffer_bloom_fill_pct(void)
{
    xSemaphoreTake(s_bloom_mutex, portMAX_DELAY);
    uint32_t set = 0;
    for (uint32_t i = 0; i < sizeof(s_bloom); i++) {
        /* popcount byte */
        uint8_t b = s_bloom[i];
        while (b) { set++; b &= (b - 1); }
    }
    xSemaphoreGive(s_bloom_mutex);
    return (float)set / (float)BLOOM_BITS * 100.0f;
}

void packet_buffer_bloom_maintain(void)
{
    int64_t now = esp_timer_get_time();
    int64_t elapsed = (now - s_bloom_last_clear) / 1000000LL;

    if (elapsed >= BLOOM_TTL_SEC) {
        xSemaphoreTake(s_bloom_mutex, portMAX_DELAY);
        memset(s_bloom, 0, sizeof(s_bloom));
        s_bloom_set_count = 0;
        s_bloom_last_clear = now;
        xSemaphoreGive(s_bloom_mutex);
        ESP_LOGI(TAG, "Bloom filter cleared (TTL %ds expired)", BLOOM_TTL_SEC);
    }
}
