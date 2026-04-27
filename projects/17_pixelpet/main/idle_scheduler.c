/*
 * idle_scheduler.c — multi-clock idle scheduler (R6).
 *
 * The pet's body anim handles the "breath" channel intrinsically (uneven
 * frame holds). What's missing from a static idle loop is the occasional
 * yawn/stretch beat — the things that tell the player "this thing is
 * alive, not just looping." That's the big-idle picker.
 *
 * One LVGL timer (250 ms granularity) tracks elapsed-since-last-pick.
 * When the gap reaches the mood-conditioned threshold, the picker draws
 * from a weighted bag (no consecutive repeats) and asks the renderer to
 * play the pick as a one-shot body anim. The renderer restores the mood
 * anim once the one-shot finishes its single cycle.
 *
 * Suppressed cases:
 *   - egg or dead stage
 *   - sleeping
 *   - no body anim available (e.g. asset bundle missing)
 *
 * Mood-conditioned tempo (gap before next big-idle):
 *   HAPPY    30 s
 *   PLAYING  30 s
 *   NEUTRAL  60 s
 *   SAD/TIRED/HUNGRY   90 s
 *   SICK     suppressed (only sway via the body anim itself)
 */

#include "idle_scheduler.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "idle_sched";

#define TICK_MS                250

typedef struct {
    const char *anim_name;     /* without species suffix; renderer adds it */
} bigidle_pick_t;

/* Big-idle bag — refills when empty, never picks the same item twice in a
 * row. Yawn + stretch are the bundled set; add sniff/scratch later when
 * those sprites exist. */
static const bigidle_pick_t BIGIDLE_BAG[] = {
    { "yawn"    },
    { "stretch" },
};
#define BIGIDLE_COUNT ((int)(sizeof(BIGIDLE_BAG) / sizeof(BIGIDLE_BAG[0])))

static struct {
    uint32_t       elapsed_ms;
    uint32_t       gap_ms;
    bool           paused;
    bool           silenced;       /* true when egg/dead/sleeping */
    int            last_pick;      /* -1 = no previous */
    uint8_t        bag[BIGIDLE_COUNT];
    uint8_t        bag_len;
} s = { .last_pick = -1 };

static lv_timer_t *s_timer;

static uint32_t mood_gap_ms(pet_mood_t mood)
{
    switch (mood) {
    case MOOD_HAPPY:
    case MOOD_PLAYING:  return 30 * 1000;
    case MOOD_NEUTRAL:  return 60 * 1000;
    case MOOD_SAD:
    case MOOD_TIRED:
    case MOOD_HUNGRY:   return 90 * 1000;
    case MOOD_SICK:
    case MOOD_SLEEPING:
    case MOOD_DEAD:
    default:            return 0;        /* suppressed */
    }
}

static void refill_bag(void)
{
    for (int i = 0; i < BIGIDLE_COUNT; i++) s.bag[i] = (uint8_t)i;
    s.bag_len = BIGIDLE_COUNT;
}

static int draw_pick(void)
{
    if (s.bag_len == 0) refill_bag();

    /* Avoid an immediate repeat by rejecting the only candidate when it
     * matches the last pick and the bag still has another option. With a
     * 2-item bag this just biases the second pick away from the first. */
    int idx = rand() % s.bag_len;
    int pick = s.bag[idx];
    if (pick == s.last_pick && s.bag_len > 1) {
        idx = (idx + 1) % s.bag_len;
        pick = s.bag[idx];
    }

    s.bag[idx] = s.bag[s.bag_len - 1];
    s.bag_len--;
    s.last_pick = pick;
    return pick;
}

static void scheduler_tick(lv_timer_t *t)
{
    (void)t;
    if (s.paused || s.silenced || s.gap_ms == 0) {
        s.elapsed_ms = 0;
        return;
    }
    s.elapsed_ms += TICK_MS;
    if (s.elapsed_ms < s.gap_ms) return;

    s.elapsed_ms = 0;
    int pick = draw_pick();
    if (pick < 0) return;
    const char *name = BIGIDLE_BAG[pick].anim_name;
    ESP_LOGI(TAG, "big-idle pick: %s", name);
    pet_renderer_play_oneshot_anim(name);
}

void idle_scheduler_init(void)
{
    refill_bag();
    s.elapsed_ms = 0;
    s.gap_ms     = mood_gap_ms(MOOD_NEUTRAL);
    s.silenced   = false;
    s.paused     = false;
    s.last_pick  = -1;
    s_timer = lv_timer_create(scheduler_tick, TICK_MS, NULL);
    ESP_LOGI(TAG, "idle scheduler ready (bag=%d)", BIGIDLE_COUNT);
}

void idle_scheduler_update(const pet_state_t *p)
{
    if (!p) return;
    bool silenced = (p->stage == STAGE_EGG) ||
                    (p->stage == STAGE_DEAD) ||
                    p->is_sleeping;
    pet_mood_t mood = pet_renderer_derive_mood(p);
    uint32_t gap = silenced ? 0 : mood_gap_ms(mood);
    if (gap != s.gap_ms) {
        /* Tempo shift — keep the elapsed counter so a transition out of a
         * suppressed mood doesn't immediately fire (avoids the "wakes up
         * and immediately yawns" edge). */
        s.gap_ms = gap;
        if (s.elapsed_ms > gap && gap > 0) s.elapsed_ms = gap - TICK_MS;
    }
    s.silenced = silenced;
}

void idle_scheduler_pause(bool paused)
{
    s.paused = paused;
    if (paused) s.elapsed_ms = 0;
}
