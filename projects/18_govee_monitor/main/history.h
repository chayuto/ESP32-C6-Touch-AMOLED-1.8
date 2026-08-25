#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "sdkconfig.h"
#include "slot_store.h"

/*
 * Rolling in-RAM history for each sensor slot.
 *
 * Three tiers are kept per sensor, each a fixed-size ring of HISTORY_POINTS
 * buckets. Every tier is fed from the same advertisement stream but averages
 * over a different bucket period, so a short range is not resampled from
 * coarse data (a 1 h view rebuilt from 12-minute buckets would be 5 points).
 *
 *   tier    window   bucket   points
 *   1H        1 h      30 s     120
 *   6H        6 h       3 min   120
 *   24H      24 h      12 min   120
 *
 * Cost is 4 bytes per bucket, so ~6 KB for 4 sensors x 3 tiers — small enough
 * that tiering is cheaper than the arithmetic to avoid it.
 *
 * History lives in RAM only and does not survive a reboot.
 */

#define HISTORY_POINTS 120

/* Bucket value meaning "no advert arrived during this bucket". Charts must
 * skip these rather than plot them as zero. */
#define HISTORY_NO_DATA INT16_MIN

typedef enum {
    HISTORY_RANGE_1H = 0,
    HISTORY_RANGE_6H,
    HISTORY_RANGE_24H,
    HISTORY_RANGE_COUNT,
} history_range_t;

typedef struct {
    int16_t temp_cx100;   /* °C x100, or HISTORY_NO_DATA */
    int16_t humid_x100;   /* %RH x100, or HISTORY_NO_DATA */
} history_point_t;

/* Zeroes every tier and anchors the first bucket to the current time. */
void history_init(void);

/* Fold one reading into the open bucket of every tier for this slot.
 * Called for each decoded advertisement. */
void history_record(int slot, float temp_c, float humid_pct);

/* Close any buckets whose period has elapsed, back-filling HISTORY_NO_DATA
 * for buckets that saw no adverts. Must be called at least once per shortest
 * bucket period (30 s), independently of whether the UI is drawing. */
void history_tick(void);

/* Copy one tier oldest-first into out[HISTORY_POINTS]. Buckets never written
 * read back as HISTORY_NO_DATA, so the newest sample is always the last
 * element and the x-axis stays anchored to "now" at the right edge.
 * Returns the number of buckets holding real data. */
uint16_t history_snapshot(int slot, history_range_t range, history_point_t *out);

/* Bucket period in seconds, for axis labelling. */
uint32_t history_bucket_seconds(history_range_t range);

/* Seconds covered by a full tier (bucket period x HISTORY_POINTS). */
uint32_t history_window_seconds(history_range_t range);

/* Test seams: the pure-logic entry points, driven by an explicit clock so the
 * ring/rollover behaviour can be exercised off-target. */
void history_init_at(int64_t now_us);
void history_record_at(int slot, float temp_c, float humid_pct, int64_t now_us);
void history_tick_at(int64_t now_us);
