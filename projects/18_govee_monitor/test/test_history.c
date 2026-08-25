#include "history.h"
#include <stdio.h>
#include <string.h>

int64_t esp_timer_get_time(void) { return 0; }

static int fails = 0;
#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { printf("FAIL %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); fails++; } \
} while (0)

#define SEC(x) ((int64_t)(x) * 1000000LL)

static history_point_t buf[HISTORY_POINTS];

static void test_windows(void)
{
    CHECK(history_window_seconds(HISTORY_RANGE_1H)  == 3600,  "1h window");
    CHECK(history_window_seconds(HISTORY_RANGE_6H)  == 21600, "6h window");
    CHECK(history_window_seconds(HISTORY_RANGE_24H) == 86400, "24h window");
}

/* A closed bucket holds the mean of the adverts folded into it, and lands as
 * the newest (last) element of the snapshot. */
static void test_bucket_average(void)
{
    history_init_at(0);
    history_record_at(0, 20.0f, 70.0f, SEC(1));
    history_record_at(0, 22.0f, 80.0f, SEC(2));
    history_tick_at(SEC(31));                       /* close the 30 s bucket */

    uint16_t n = history_snapshot(0, HISTORY_RANGE_1H, buf);
    CHECK(n == 1, "expected 1 valid bucket, got %u", n);
    CHECK(buf[HISTORY_POINTS - 1].temp_cx100 == 2100, "temp avg = %d",
          buf[HISTORY_POINTS - 1].temp_cx100);
    CHECK(buf[HISTORY_POINTS - 1].humid_x100 == 7500, "humid avg = %d",
          buf[HISTORY_POINTS - 1].humid_x100);
    CHECK(buf[HISTORY_POINTS - 2].temp_cx100 == HISTORY_NO_DATA, "older bucket empty");

    /* Coarser tiers are still accumulating — nothing closed yet. */
    CHECK(history_snapshot(0, HISTORY_RANGE_6H, buf) == 0, "6h still open");
    CHECK(history_snapshot(0, HISTORY_RANGE_24H, buf) == 0, "24h still open");
}

/* Silent buckets must back-fill as NO_DATA so the x-axis keeps its shape. */
static void test_gap_backfill(void)
{
    history_init_at(0);
    history_record_at(0, 20.0f, 50.0f, SEC(1));
    history_tick_at(SEC(31));            /* bucket 1: real */
    history_tick_at(SEC(151));           /* buckets 2..5: silent */

    uint16_t n = history_snapshot(0, HISTORY_RANGE_1H, buf);
    CHECK(n == 1, "only one real bucket, got %u", n);
    CHECK(buf[HISTORY_POINTS - 1].temp_cx100 == HISTORY_NO_DATA, "newest is a gap");
    CHECK(buf[HISTORY_POINTS - 5].temp_cx100 == 2000, "real bucket 5 back, got %d",
          buf[HISTORY_POINTS - 5].temp_cx100);
}

/* Ring wrap: after more than HISTORY_POINTS buckets the oldest fall off and
 * the newest stays pinned to the right edge. */
static void test_ring_wrap(void)
{
    history_init_at(0);
    for (int i = 0; i < HISTORY_POINTS + 10; i++) {
        history_record_at(0, (float)i, 50.0f, SEC(i * 30 + 1));
        history_tick_at(SEC((i + 1) * 30 + 1));
    }
    uint16_t n = history_snapshot(0, HISTORY_RANGE_1H, buf);
    CHECK(n == HISTORY_POINTS, "ring saturates at %d, got %u", HISTORY_POINTS, n);
    CHECK(buf[HISTORY_POINTS - 1].temp_cx100 == (HISTORY_POINTS + 9) * 100,
          "newest = %d", buf[HISTORY_POINTS - 1].temp_cx100);
    CHECK(buf[0].temp_cx100 == 10 * 100, "oldest surviving = %d", buf[0].temp_cx100);
}

/* A gap longer than the whole window clears the ring instead of looping over
 * hours of missed buckets. */
static void test_gap_longer_than_window(void)
{
    history_init_at(0);
    history_record_at(0, 20.0f, 50.0f, SEC(1));
    history_tick_at(SEC(31));
    history_tick_at(SEC(3600 * 5));      /* 5 h of silence on a 1 h tier */

    uint16_t n = history_snapshot(0, HISTORY_RANGE_1H, buf);
    CHECK(n == 0, "ring cleared, got %u valid", n);
    for (int i = 0; i < HISTORY_POINTS; i++) {
        CHECK(buf[i].temp_cx100 == HISTORY_NO_DATA, "slot %d cleared", i);
    }
    /* And it must keep working afterwards. */
    history_record_at(0, 25.0f, 60.0f, SEC(3600 * 5 + 1));
    history_tick_at(SEC(3600 * 5 + 31));
    CHECK(history_snapshot(0, HISTORY_RANGE_1H, buf) == 1, "recovers after gap");
    CHECK(buf[HISTORY_POINTS - 1].temp_cx100 == 2500, "post-gap value");
}

/* Slots are independent; out-of-range slots are ignored, not written. */
static void test_slot_isolation(void)
{
    history_init_at(0);
    history_record_at(0, 20.0f, 50.0f, SEC(1));
    history_record_at(1, 30.0f, 90.0f, SEC(1));
    history_record_at(99, 40.0f, 10.0f, SEC(1));     /* must be a no-op */
    history_tick_at(SEC(31));

    history_snapshot(0, HISTORY_RANGE_1H, buf);
    CHECK(buf[HISTORY_POINTS - 1].temp_cx100 == 2000, "slot 0 kept its own value");
    history_snapshot(1, HISTORY_RANGE_1H, buf);
    CHECK(buf[HISTORY_POINTS - 1].temp_cx100 == 3000, "slot 1 kept its own value");
    CHECK(history_snapshot(99, HISTORY_RANGE_1H, buf) == 0, "bad slot returns empty");
}

/* The 24 h tier averages across its whole 12-minute bucket. */
static void test_coarse_tier(void)
{
    history_init_at(0);
    for (int i = 0; i < 24; i++) {                   /* one advert every 30 s */
        history_record_at(0, 10.0f + i, 50.0f, SEC(i * 30 + 1));
    }
    history_tick_at(SEC(721));                       /* close the 12 min bucket */
    uint16_t n = history_snapshot(0, HISTORY_RANGE_24H, buf);
    CHECK(n == 1, "24h bucket closed, got %u", n);
    /* mean of 10..33 = 21.5 */
    CHECK(buf[HISTORY_POINTS - 1].temp_cx100 == 2150, "24h avg = %d",
          buf[HISTORY_POINTS - 1].temp_cx100);
}

int main(void)
{
    test_windows();
    test_bucket_average();
    test_gap_backfill();
    test_ring_wrap();
    test_gap_longer_than_window();
    test_slot_isolation();
    test_coarse_tier();

    if (fails == 0) {
        printf("all history tests passed\n");
        return 0;
    }
    printf("%d check(s) failed\n", fails);
    return 1;
}
