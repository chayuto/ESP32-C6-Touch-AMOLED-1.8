#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "sdkconfig.h"

#define SLOT_STORE_MAX 4   /* Compile-time upper bound; runtime uses CONFIG_GOVEE_MAX_SLOTS */

typedef struct {
    bool        valid;
    bool        pinned;         /* True if slot index < GOVEE_KNOWN_COUNT */
    bool        stale;
    uint8_t     mac[6];         /* Big-endian (sticker order) */
    char        label[16];
    float       temp_c;
    float       humid_pct;
    uint8_t     battery_pct;
    int8_t      rssi;
    int8_t      rssi_ewma;      /* Smoothed RSSI for future rotation logic */
    int64_t     last_seen_us;
} slot_t;

void slot_store_init(void);

/* Snapshot all slots for UI consumption. */
void slot_store_snapshot(slot_t *out);

/* Refresh stale flag and evict expired unpinned slots. */
void slot_store_tick(void);

/* Phase-1 helper: load three placeholder rows so we can validate the UI
 * before any BLE wiring. Removed once the scanner feeds real data. */
void slot_store_load_placeholders(void);
