/*
 * telemetry.c — Thread-safe counters and mesh telemetry aggregation
 *
 * BLE task calls telemetry_on_*() to increment counters.
 * LVGL timer calls telemetry_get_snapshot() to read a consistent copy.
 * telemetry_update() computes derived fields (packets/sec, uptime, heap).
 */

#include "telemetry.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static telemetry_t s_telem;
static SemaphoreHandle_t s_mutex;

/* For packets/sec calculation */
static uint32_t s_prev_rx;
static int64_t  s_prev_time;

void telemetry_init(void)
{
    memset(&s_telem, 0, sizeof(s_telem));
    s_mutex = xSemaphoreCreateMutex();
    s_prev_rx = 0;
    s_prev_time = esp_timer_get_time();
}

/* ── Size bucket index ───────────────────────────────────── */

static int size_bucket(uint16_t size)
{
    if (size <= 256)  return 0;
    if (size <= 512)  return 1;
    if (size <= 1024) return 2;
    return 3;
}

/* ── Peer tracking ───────────────────────────────────────── */

static void update_peer(const uint8_t *peer_id, int8_t rssi)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* Find existing */
    for (int i = 0; i < TELEMETRY_MAX_PEERS; i++) {
        if (s_telem.peers[i].active &&
            memcmp(s_telem.peers[i].peer_id, peer_id, 8) == 0) {
            s_telem.peers[i].rssi = rssi;
            s_telem.peers[i].last_seen_ms = now_ms;
            s_telem.peers[i].packets_from++;
            return;
        }
    }

    /* Find empty slot */
    for (int i = 0; i < TELEMETRY_MAX_PEERS; i++) {
        if (!s_telem.peers[i].active) {
            memcpy(s_telem.peers[i].peer_id, peer_id, 8);
            s_telem.peers[i].rssi = rssi;
            s_telem.peers[i].last_seen_ms = now_ms;
            s_telem.peers[i].packets_from = 1;
            s_telem.peers[i].active = true;
            s_telem.peer_count_total++;
            return;
        }
    }

    /* Full — evict oldest */
    int oldest_idx = 0;
    uint32_t oldest_time = UINT32_MAX;
    for (int i = 0; i < TELEMETRY_MAX_PEERS; i++) {
        if (s_telem.peers[i].last_seen_ms < oldest_time) {
            oldest_time = s_telem.peers[i].last_seen_ms;
            oldest_idx = i;
        }
    }
    memcpy(s_telem.peers[oldest_idx].peer_id, peer_id, 8);
    s_telem.peers[oldest_idx].rssi = rssi;
    s_telem.peers[oldest_idx].last_seen_ms = now_ms;
    s_telem.peers[oldest_idx].packets_from = 1;
}

/* ── Increment functions (called from BLE task) ──────────── */

void telemetry_on_packet_rx(const uint8_t *peer_id, int8_t rssi,
                            const parsed_header_t *hdr, uint16_t raw_size)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_telem.packets_rx++;
    if (hdr) {
        uint8_t ttl = hdr->ttl;
        if (ttl < TELEMETRY_TTL_BUCKETS) {
            s_telem.ttl_histogram[ttl]++;
        }
        /* Message type distribution */
        uint8_t mt = hdr->msg_type;
        if (mt < 7) {
            s_telem.msg_type_counts[mt]++;
        } else {
            s_telem.msg_type_counts[7]++; /* other */
        }
        /* Broadcast vs directed */
        if (hdr->is_broadcast || !hdr->has_recipient) {
            s_telem.broadcasts++;
        } else {
            s_telem.directed++;
        }
        if (hdr->has_signature) {
            s_telem.signed_packets++;
        }
        /* Track sender ID as peer */
        update_peer(hdr->sender_id, rssi);
    } else if (peer_id) {
        update_peer(peer_id, rssi);
    }
    s_telem.size_histogram[size_bucket(raw_size)]++;
    xSemaphoreGive(s_mutex);
}

void telemetry_on_packet_tx(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_telem.packets_tx++;
    xSemaphoreGive(s_mutex);
}

void telemetry_on_bloom_hit(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_telem.bloom_hits++;
    s_telem.packets_dropped++;
    xSemaphoreGive(s_mutex);
}

void telemetry_on_bloom_miss(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_telem.bloom_misses++;
    xSemaphoreGive(s_mutex);
}

void telemetry_on_packet_expired(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_telem.packets_expired++;
    xSemaphoreGive(s_mutex);
}

void telemetry_update_peer_nickname(const uint8_t *peer_id, const char *nickname)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < TELEMETRY_MAX_PEERS; i++) {
        if (s_telem.peers[i].active &&
            memcmp(s_telem.peers[i].peer_id, peer_id, 8) == 0) {
            strncpy(s_telem.peers[i].nickname, nickname, PEER_NICKNAME_MAX - 1);
            s_telem.peers[i].nickname[PEER_NICKNAME_MAX - 1] = '\0';
            break;
        }
    }
    xSemaphoreGive(s_mutex);
}

void telemetry_on_fragment(uint8_t type)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_telem.fragments_rx++;
    if (type == 0) s_telem.fragment_starts++;
    else if (type == 2) s_telem.fragment_ends++;
    xSemaphoreGive(s_mutex);
}

void telemetry_on_sd_flush(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_telem.sd_flush_count++;
    xSemaphoreGive(s_mutex);
}

void telemetry_set_spiffs_rows(uint32_t rows)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_telem.spiffs_rows = rows;
    xSemaphoreGive(s_mutex);
}

void telemetry_set_bloom_fill(float pct)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_telem.bloom_fill_pct = pct;
    xSemaphoreGive(s_mutex);
}

void telemetry_set_packets_buffered(uint32_t count)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_telem.packets_buffered = count;
    xSemaphoreGive(s_mutex);
}

/* ── Periodic update (call from LVGL timer or main loop) ── */

void telemetry_update(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    int64_t now = esp_timer_get_time();
    s_telem.uptime_sec = (uint32_t)(now / 1000000LL);
    s_telem.free_heap = esp_get_free_heap_size();
    s_telem.min_free_heap = esp_get_minimum_free_heap_size();

    /* Packets/sec: rolling calculation */
    int64_t dt_us = now - s_prev_time;
    if (dt_us > 1000000) {  /* At least 1 second elapsed */
        uint32_t dpkt = s_telem.packets_rx - s_prev_rx;
        float dt_sec = (float)dt_us / 1000000.0f;
        /* Exponential moving average */
        float instant = (float)dpkt / dt_sec;
        s_telem.packets_per_sec = s_telem.packets_per_sec * 0.7f + instant * 0.3f;
        s_prev_rx = s_telem.packets_rx;
        s_prev_time = now;
    }

    /* Count active peers (seen in last 60 seconds) */
    uint32_t now_ms = (uint32_t)(now / 1000);
    uint8_t active = 0;
    for (int i = 0; i < TELEMETRY_MAX_PEERS; i++) {
        if (s_telem.peers[i].active) {
            if (now_ms - s_telem.peers[i].last_seen_ms > 60000) {
                s_telem.peers[i].active = false;
            } else {
                active++;
            }
        }
    }
    s_telem.peer_count = active;

    xSemaphoreGive(s_mutex);
}

void telemetry_get_snapshot(telemetry_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(out, &s_telem, sizeof(telemetry_t));
    xSemaphoreGive(s_mutex);
}
