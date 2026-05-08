#include "slot_store.h"
#include "device_config.h"

#include <string.h>
#include <stdio.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define ROTATION_HYSTERESIS_DB  6   /* New signal must beat weakest by >= 6 dB */
#define RSSI_EWMA_NUM           3   /* α = 0.3 → new = (3·now + 7·prev) / 10 */
#define RSSI_EWMA_DEN           10

static slot_t            s_slots[SLOT_STORE_MAX];
static SemaphoreHandle_t s_lock;

static int active_slots(void)
{
    int n = CONFIG_GOVEE_MAX_SLOTS;
    if (n > SLOT_STORE_MAX) n = SLOT_STORE_MAX;
    return n;
}

static void label_default(char *dst, size_t dst_len,
                          const char *broadcast_name,
                          const uint8_t mac_be[6])
{
    /* Prefer the broadcast local name (e.g. "GVH5075_8C9A") so the tile shows
     * the device's self-reported ID verbatim. Fall back to a MAC-derived
     * short label if the advert didn't carry a name. */
    if (broadcast_name && broadcast_name[0]) {
        strncpy(dst, broadcast_name, dst_len - 1);
        dst[dst_len - 1] = '\0';
    } else {
        snprintf(dst, dst_len, "H5075-%02X%02X", mac_be[4], mac_be[5]);
    }
}

static int find_pinned(const uint8_t mac_be[6])
{
    int n = active_slots();
    for (int i = 0; i < n; i++) {
        if (s_slots[i].pinned && memcmp(s_slots[i].mac, mac_be, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_existing_auto(const uint8_t mac_be[6])
{
    int n = active_slots();
    for (int i = 0; i < n; i++) {
        if (!s_slots[i].pinned && s_slots[i].valid &&
            memcmp(s_slots[i].mac, mac_be, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static int first_free_auto(void)
{
    int n = active_slots();
    for (int i = 0; i < n; i++) {
        if (!s_slots[i].pinned && !s_slots[i].valid) {
            return i;
        }
    }
    return -1;
}

static int weakest_auto(int8_t *out_rssi_ewma)
{
    int weakest = -1;
    int8_t weakest_rssi = 127;
    int n = active_slots();
    for (int i = 0; i < n; i++) {
        if (!s_slots[i].pinned && s_slots[i].valid) {
            if (s_slots[i].rssi_ewma < weakest_rssi) {
                weakest_rssi = s_slots[i].rssi_ewma;
                weakest = i;
            }
        }
    }
    if (weakest >= 0 && out_rssi_ewma) *out_rssi_ewma = weakest_rssi;
    return weakest;
}

static void apply_reading(int idx,
                          const uint8_t mac_be[6],
                          const char *broadcast_name,
                          const govee_reading_t *r,
                          int8_t rssi,
                          bool first_time)
{
    slot_t *s = &s_slots[idx];

    if (first_time) {
        memcpy(s->mac, mac_be, 6);
        if (!s->pinned) {
            label_default(s->label, sizeof(s->label), broadcast_name, mac_be);
        }
        s->rssi_ewma = rssi;
    } else {
        /* EWMA: new = α·now + (1-α)·prev */
        int32_t mixed = (int32_t)RSSI_EWMA_NUM * rssi +
                        (int32_t)(RSSI_EWMA_DEN - RSSI_EWMA_NUM) * s->rssi_ewma;
        s->rssi_ewma = (int8_t)(mixed / RSSI_EWMA_DEN);
    }
    s->valid        = true;
    s->stale        = false;
    s->temp_c       = r->temp_c;
    s->humid_pct    = r->humid_pct;
    s->battery_pct  = r->battery_pct;
    s->rssi         = rssi;
    s->last_seen_us = esp_timer_get_time();
}

void slot_store_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    memset(s_slots, 0, sizeof(s_slots));

    int n = active_slots();
    int known = (int)GOVEE_KNOWN_COUNT;     /* int cast silences i<0 warning when empty */
    for (int i = 0; i < known && i < n; i++) {
        s_slots[i].pinned = true;
        memcpy(s_slots[i].mac, GOVEE_KNOWN[i].mac, 6);
        strncpy(s_slots[i].label, GOVEE_KNOWN[i].label,
                sizeof(s_slots[i].label) - 1);
    }
    /* Suppress unused-warning when GOVEE_KNOWN[] is empty. */
    (void)GOVEE_KNOWN;
}

void slot_store_snapshot(slot_t *out)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(out, s_slots, sizeof(slot_t) * active_slots());
    if (s_lock) xSemaphoreGive(s_lock);
}

void slot_store_tick(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);

    int64_t now = esp_timer_get_time();
    int64_t stale_us = (int64_t)CONFIG_GOVEE_STALE_TIMEOUT_S * 1000000LL;
    int64_t evict_us = (int64_t)CONFIG_GOVEE_EVICT_TIMEOUT_S * 1000000LL;
    int n = active_slots();

    for (int i = 0; i < n; i++) {
        if (!s_slots[i].valid) continue;
        int64_t age = now - s_slots[i].last_seen_us;
        s_slots[i].stale = (age > stale_us);
        if (!s_slots[i].pinned && age > evict_us) {
            /* Free unpinned slot for a future device. */
            memset(&s_slots[i], 0, sizeof(slot_t));
        }
    }

    if (s_lock) xSemaphoreGive(s_lock);
}

bool slot_store_update(const uint8_t mac_be[6],
                       const char    *broadcast_name,
                       const govee_reading_t *r,
                       int8_t rssi)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    bool stored = false;

    /* (1) Pinned slot wins. */
    int idx = find_pinned(mac_be);
    if (idx >= 0) {
        apply_reading(idx, mac_be, broadcast_name, r, rssi, !s_slots[idx].valid);
        stored = true;
        goto out;
    }

    /* (2) Already tracked in an auto slot? */
    idx = find_existing_auto(mac_be);
    if (idx >= 0) {
        apply_reading(idx, mac_be, broadcast_name, r, rssi, false);
        stored = true;
        goto out;
    }

    /* (3) Free auto slot? */
    idx = first_free_auto();
    if (idx >= 0) {
        apply_reading(idx, mac_be, broadcast_name, r, rssi, true);
        stored = true;
        goto out;
    }

    /* (4) Rotate weakest if new RSSI beats it by hysteresis margin. */
    int8_t weakest_rssi = 0;
    idx = weakest_auto(&weakest_rssi);
    if (idx >= 0 && rssi > weakest_rssi + ROTATION_HYSTERESIS_DB) {
        memset(&s_slots[idx], 0, sizeof(slot_t));
        apply_reading(idx, mac_be, broadcast_name, r, rssi, true);
        stored = true;
    }

out:
    if (s_lock) xSemaphoreGive(s_lock);
    return stored;
}
