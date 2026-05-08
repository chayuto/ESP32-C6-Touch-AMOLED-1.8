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
    { {0xA4, 0xC1, 0x38, 0xDE, 0x38, 0x9B}, "Living room" },   // GVH5075_389B
    /* Discovered but unlabelled — uncomment and assign as you identify each:
     * { {0xA4, 0xC1, 0x38, 0x1A, 0x8C, 0x9A}, "Room B" },     // GVH5075_8C9A
     * { {0xA4, 0xC1, 0x38, 0x5C, 0x15, 0xF1}, "Room C" },     // GVH5075_15F1 (low battery)
     */
};

#define GOVEE_KNOWN_COUNT (sizeof(GOVEE_KNOWN) / sizeof(GOVEE_KNOWN[0]))
