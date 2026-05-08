#pragma once

#include <stdint.h>

/*
 * Compile-time list of pinned Govee sensors.
 *
 * Each entry binds a fixed BLE MAC address (as printed on the sensor sticker,
 * big-endian byte order) to a friendly display label.
 *
 * Empty table  -> pure auto-discovery: the first GOVEE_MAX_SLOTS H5075s seen
 *                 fill the slots by RSSI; labels default to "H5075-XXXX"
 *                 (last 4 hex of MAC). Stronger signals can rotate weaker ones
 *                 out (6 dB hysteresis).
 *
 * Non-empty    -> listed MACs are pinned to slot indices 0..N-1 with the given
 *                 labels. Pinned slots never rotate or evict; they grey out
 *                 if stale but stay reserved. Remaining slots auto-fill.
 *
 * Edit and rebuild to apply.
 */

typedef struct {
    uint8_t     mac[6];   /* Big-endian — same byte order as printed on sticker */
    const char *label;    /* Up to ~12 chars looks good on screen */
} govee_known_device_t;

static const govee_known_device_t GOVEE_KNOWN[] = {
    /* Examples — uncomment + replace with real MACs from sensor stickers:
     * { {0xA4, 0xC1, 0x38, 0x12, 0x34, 0x56}, "Bedroom" },
     * { {0xA4, 0xC1, 0x38, 0x78, 0x9A, 0xBC}, "Kitchen" },
     * { {0xA4, 0xC1, 0x38, 0xDE, 0xF0, 0x12}, "Outside" },
     */
};

#define GOVEE_KNOWN_COUNT (sizeof(GOVEE_KNOWN) / sizeof(GOVEE_KNOWN[0]))
