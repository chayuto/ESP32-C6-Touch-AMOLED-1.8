#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "sdkconfig.h"
#include "govee_decoder.h"

#define SLOT_STORE_MAX 4   /* Compile-time upper bound; runtime uses CONFIG_GOVEE_MAX_SLOTS */

typedef struct {
    bool        valid;          /* True once a sensor is bound to this slot */
    bool        pinned;         /* True if slot index < GOVEE_KNOWN_COUNT */
    bool        stale;          /* True if no advert within STALE_TIMEOUT */
    uint8_t     mac[6];         /* Big-endian (sticker order) */
    char        label[16];
    float       temp_c;
    float       humid_pct;
    uint8_t     battery_pct;
    int8_t      rssi;           /* Most recent RSSI (raw) */
    int8_t      rssi_ewma;      /* Smoothed RSSI for rotation decisions */
    int64_t     last_seen_us;
} slot_t;

/* Init: zeroes slots, reserves the first GOVEE_KNOWN_COUNT as pinned with their
 * configured labels. Must be called before any other slot_store function. */
void slot_store_init(void);

/* Snapshot all slots into out[]. Buffer must hold CONFIG_GOVEE_MAX_SLOTS entries.
 * Safe to call from any task — takes the internal mutex. */
void slot_store_snapshot(slot_t *out);

/* Update bookkeeping — marks stale, frees evicted unpinned slots.
 * Call ~1 Hz from a single thread. */
void slot_store_tick(void);

/* (Phase-1 helper has been removed — the BLE scanner now feeds the store.) */

/* Route a fresh decoded reading into the store.
 *   - If MAC matches a pinned entry  → write into that pinned slot.
 *   - Else if MAC already tracked in an auto slot → update it.
 *   - Else if a free auto slot exists → claim it.
 *   - Else if the new RSSI beats the weakest auto slot by >=6 dB → rotate.
 *   - Else ignore.
 * Returns true if the reading was stored (so the caller can log accordingly). */
bool slot_store_update(const uint8_t mac_be[6],
                       const char    *broadcast_name,   /* e.g. "GVH5075_8C9A"; NULL → fall back to MAC */
                       const govee_reading_t *r,
                       int8_t rssi);
