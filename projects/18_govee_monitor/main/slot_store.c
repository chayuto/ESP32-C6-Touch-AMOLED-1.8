#include "slot_store.h"
#include "device_config.h"

#include <string.h>
#include <stdio.h>

#include "esp_timer.h"

static slot_t s_slots[SLOT_STORE_MAX];

static int active_slots(void)
{
    int n = CONFIG_GOVEE_MAX_SLOTS;
    if (n > SLOT_STORE_MAX) n = SLOT_STORE_MAX;
    return n;
}

void slot_store_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));

    int n = active_slots();
    int known = (int)GOVEE_KNOWN_COUNT;     /* int cast silences i<0 warning when empty */
    for (int i = 0; i < known && i < n; i++) {
        s_slots[i].pinned = true;
        memcpy(s_slots[i].mac, GOVEE_KNOWN[i].mac, 6);
        strncpy(s_slots[i].label, GOVEE_KNOWN[i].label,
                sizeof(s_slots[i].label) - 1);
    }
    (void)GOVEE_KNOWN;
}

void slot_store_snapshot(slot_t *out)
{
    memcpy(out, s_slots, sizeof(slot_t) * active_slots());
}

void slot_store_tick(void)
{
    int64_t now = esp_timer_get_time();
    int64_t stale_us = (int64_t)CONFIG_GOVEE_STALE_TIMEOUT_S * 1000000LL;
    int64_t evict_us = (int64_t)CONFIG_GOVEE_EVICT_TIMEOUT_S * 1000000LL;
    int n = active_slots();

    for (int i = 0; i < n; i++) {
        if (!s_slots[i].valid) continue;
        int64_t age = now - s_slots[i].last_seen_us;
        s_slots[i].stale = (age > stale_us);
        if (!s_slots[i].pinned && age > evict_us) {
            memset(&s_slots[i], 0, sizeof(slot_t));
        }
    }
}

void slot_store_load_placeholders(void)
{
    int64_t now = esp_timer_get_time();

    s_slots[0].valid = true;
    s_slots[0].pinned = false;
    s_slots[0].stale = false;
    snprintf(s_slots[0].label, sizeof(s_slots[0].label), "Bedroom");
    s_slots[0].temp_c = 21.6f;
    s_slots[0].humid_pct = 49.8f;
    s_slots[0].battery_pct = 100;
    s_slots[0].rssi = -52;
    s_slots[0].rssi_ewma = -52;
    s_slots[0].last_seen_us = now;

    if (CONFIG_GOVEE_MAX_SLOTS > 1) {
        s_slots[1].valid = true;
        snprintf(s_slots[1].label, sizeof(s_slots[1].label), "Kitchen");
        s_slots[1].temp_c = 24.1f;
        s_slots[1].humid_pct = 42.0f;
        s_slots[1].battery_pct = 78;
        s_slots[1].rssi = -64;
        s_slots[1].rssi_ewma = -64;
        s_slots[1].last_seen_us = now;
    }

    if (CONFIG_GOVEE_MAX_SLOTS > 2) {
        s_slots[2].valid = true;
        snprintf(s_slots[2].label, sizeof(s_slots[2].label), "Outside");
        s_slots[2].temp_c = -3.4f;
        s_slots[2].humid_pct = 88.2f;
        s_slots[2].battery_pct = 55;
        s_slots[2].rssi = -78;
        s_slots[2].rssi_ewma = -78;
        s_slots[2].last_seen_us = now;
    }
}
