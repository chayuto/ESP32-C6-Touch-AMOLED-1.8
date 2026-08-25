#include "history.h"

#include <string.h>
#include <limits.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Bucket period per tier. HISTORY_POINTS buckets of these lengths give
 * exactly 1 h / 6 h / 24 h of coverage. */
static const uint32_t s_bucket_s[HISTORY_RANGE_COUNT] = { 30, 180, 720 };

typedef struct {
    history_point_t pts[HISTORY_POINTS];
    uint8_t  head;            /* next write index; pts[head] is the oldest */
    uint8_t  count;           /* buckets holding real data, saturates */
    int32_t  sum_tx100;       /* open-bucket accumulator */
    int32_t  sum_hx100;
    uint16_t n;               /* adverts folded into the open bucket */
    int64_t  bucket_end_us;
} tier_t;

static tier_t            s_tier[SLOT_STORE_MAX][HISTORY_RANGE_COUNT];
static SemaphoreHandle_t s_lock;

static void lock(void)   { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

static int active_slots(void)
{
    int n = CONFIG_GOVEE_MAX_SLOTS;
    if (n > SLOT_STORE_MAX) n = SLOT_STORE_MAX;
    return n;
}

/* Close the open bucket: push its average, or NO_DATA if it saw nothing. */
static void tier_close_bucket(tier_t *t)
{
    history_point_t p = { HISTORY_NO_DATA, HISTORY_NO_DATA };
    if (t->n > 0) {
        p.temp_cx100 = (int16_t)(t->sum_tx100 / t->n);
        p.humid_x100 = (int16_t)(t->sum_hx100 / t->n);
        if (t->count < HISTORY_POINTS) t->count++;
    }
    t->pts[t->head] = p;
    t->head = (uint8_t)((t->head + 1) % HISTORY_POINTS);
    t->sum_tx100 = 0;
    t->sum_hx100 = 0;
    t->n = 0;
}

/* Advance a tier to `now`, back-filling every bucket that elapsed silently.
 * A gap longer than the whole window collapses to a full ring of NO_DATA
 * rather than looping for hours of missed buckets. */
static void tier_advance(tier_t *t, int64_t now_us, uint32_t period_s)
{
    int64_t period_us = (int64_t)period_s * 1000000LL;
    if (now_us < t->bucket_end_us) return;

    int64_t elapsed = now_us - t->bucket_end_us;
    int64_t missed  = elapsed / period_us + 1;   /* buckets to close */

    if (missed > HISTORY_POINTS) {
        /* Everything on record is older than the window — reset the ring. */
        for (int i = 0; i < HISTORY_POINTS; i++) {
            t->pts[i].temp_cx100 = HISTORY_NO_DATA;
            t->pts[i].humid_x100 = HISTORY_NO_DATA;
        }
        t->head = 0;
        t->count = 0;
        t->sum_tx100 = 0;
        t->sum_hx100 = 0;
        t->n = 0;
    } else {
        for (int64_t i = 0; i < missed; i++) {
            tier_close_bucket(t);
        }
    }
    t->bucket_end_us += missed * period_us;
}

void history_init_at(int64_t now_us)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    lock();
    memset(s_tier, 0, sizeof(s_tier));
    for (int s = 0; s < SLOT_STORE_MAX; s++) {
        for (int r = 0; r < HISTORY_RANGE_COUNT; r++) {
            tier_t *t = &s_tier[s][r];
            for (int i = 0; i < HISTORY_POINTS; i++) {
                t->pts[i].temp_cx100 = HISTORY_NO_DATA;
                t->pts[i].humid_x100 = HISTORY_NO_DATA;
            }
            t->bucket_end_us = now_us + (int64_t)s_bucket_s[r] * 1000000LL;
        }
    }
    unlock();
}

void history_record_at(int slot, float temp_c, float humid_pct, int64_t now_us)
{
    if (slot < 0 || slot >= active_slots()) return;

    int32_t tx100 = (int32_t)(temp_c * 100.0f);
    int32_t hx100 = (int32_t)(humid_pct * 100.0f);

    lock();
    for (int r = 0; r < HISTORY_RANGE_COUNT; r++) {
        tier_t *t = &s_tier[slot][r];
        tier_advance(t, now_us, s_bucket_s[r]);
        t->sum_tx100 += tx100;
        t->sum_hx100 += hx100;
        t->n++;
    }
    unlock();
}

void history_tick_at(int64_t now_us)
{
    int n = active_slots();
    lock();
    for (int s = 0; s < n; s++) {
        for (int r = 0; r < HISTORY_RANGE_COUNT; r++) {
            tier_advance(&s_tier[s][r], now_us, s_bucket_s[r]);
        }
    }
    unlock();
}

void history_init(void)
{
    history_init_at(esp_timer_get_time());
}

void history_record(int slot, float temp_c, float humid_pct)
{
    history_record_at(slot, temp_c, humid_pct, esp_timer_get_time());
}

void history_tick(void)
{
    history_tick_at(esp_timer_get_time());
}

uint16_t history_snapshot(int slot, history_range_t range, history_point_t *out)
{
    if (!out) return 0;
    if (slot < 0 || slot >= active_slots() || range >= HISTORY_RANGE_COUNT) {
        for (int i = 0; i < HISTORY_POINTS; i++) {
            out[i].temp_cx100 = HISTORY_NO_DATA;
            out[i].humid_x100 = HISTORY_NO_DATA;
        }
        return 0;
    }

    lock();
    const tier_t *t = &s_tier[slot][range];
    for (int i = 0; i < HISTORY_POINTS; i++) {
        out[i] = t->pts[(t->head + i) % HISTORY_POINTS];
    }
    uint16_t valid = t->count;
    unlock();
    return valid;
}

uint32_t history_bucket_seconds(history_range_t range)
{
    if (range >= HISTORY_RANGE_COUNT) return 0;
    return s_bucket_s[range];
}

uint32_t history_window_seconds(history_range_t range)
{
    if (range >= HISTORY_RANGE_COUNT) return 0;
    return s_bucket_s[range] * HISTORY_POINTS;
}
