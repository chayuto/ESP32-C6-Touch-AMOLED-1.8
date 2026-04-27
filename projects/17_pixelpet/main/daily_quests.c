/*
 * daily_quests.c — three rotating per-day objectives that reset at
 * local midnight. Reward on completion is a happy-stat boost plus a
 * story-card callout, no economy yet.
 *
 * Quests are picked at random from the QUEST_DEFS pool with no
 * duplicates. Progress lives in pet_state_t (saved with the rest of
 * the pet) so the player can pick up where they left off after a
 * power cycle.
 */

#include "daily_quests.h"
#include "audio_jingles.h"
#include "story_card.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "quests";

typedef struct {
    quest_id_t  id;
    uint8_t     target;
    const char *short_name;     /* shown on status line */
    const char *story_title;    /* shown on completion card */
    const char *story_body;
} quest_def_t;

static const quest_def_t QUEST_DEFS[] = {
    { QUEST_FEED_3,       3, "feed",    "QUEST DONE",
      "fed 3 meals today\n+5 happy" },
    { QUEST_PLAY_2,       2, "play",    "QUEST DONE",
      "played twice today\n+5 happy" },
    { QUEST_CLEAN_1,      1, "clean",   "QUEST DONE",
      "kept the bowl clean\n+5 happy" },
    { QUEST_DISCIPLINE_1, 1, "scold",   "QUEST DONE",
      "discipline today\n+5 happy" },
    { QUEST_MINIGAME_5,   5, "catch 5", "QUEST DONE",
      "caught 5 in one run\n+5 happy" },
};
#define QUEST_DEF_COUNT (int)(sizeof(QUEST_DEFS) / sizeof(QUEST_DEFS[0]))

static const quest_def_t *find_def(quest_id_t id)
{
    for (int i = 0; i < QUEST_DEF_COUNT; i++) {
        if (QUEST_DEFS[i].id == id) return &QUEST_DEFS[i];
    }
    return NULL;
}

static int local_yday(int64_t unix_s)
{
    /* Use UTC day boundaries — the board doesn't have a TZ offset wired,
     * and the only requirement is "today" maps to a stable bucket per
     * 24h. tm_year + tm_yday gives a monotonic day index (across years). */
    time_t t = (time_t)unix_s;
    struct tm tm;
    gmtime_r(&t, &tm);
    return tm.tm_year * 366 + tm.tm_yday;
}

static void roll_quests(pet_state_t *p)
{
    /* Pick three distinct quest IDs from the pool. Pool size ≥ 3. */
    int taken[QUEST_DEF_COUNT];
    for (int i = 0; i < QUEST_DEF_COUNT; i++) taken[i] = 0;

    for (int slot = 0; slot < 3; slot++) {
        int pick;
        do { pick = rand() % QUEST_DEF_COUNT; } while (taken[pick]);
        taken[pick] = 1;
        p->quest_id[slot]       = (uint8_t)QUEST_DEFS[pick].id;
        p->quest_progress[slot] = 0;
        p->quest_done[slot]     = 0;
    }
    ESP_LOGI(TAG, "rolled quests: %u %u %u",
             p->quest_id[0], p->quest_id[1], p->quest_id[2]);
}

/* 2020-01-01 UTC. Anything below this means the RTC is unset / wrong
 * and the day-bucket math (gmtime / yday) is meaningless. */
#define MIN_VALID_UNIX  1577836800LL

void daily_quests_check_reset(pet_state_t *p, int64_t now_unix)
{
    if (p->stage == STAGE_EGG || p->stage == STAGE_DEAD) return;
    if (now_unix < MIN_VALID_UNIX) return;

    bool need_roll = false;
    if (p->last_quest_reset_unix == 0) {
        need_roll = true;
    } else if (local_yday(now_unix) != local_yday(p->last_quest_reset_unix)) {
        need_roll = true;
    } else {
        /* Validate stored quest IDs in case the pool changed. */
        for (int i = 0; i < 3; i++) {
            if (p->quest_id[i] >= QUEST_COUNT || !find_def((quest_id_t)p->quest_id[i])) {
                need_roll = true;
                break;
            }
        }
    }
    if (!need_roll) return;

    roll_quests(p);
    p->last_quest_reset_unix = now_unix;
    /* Tell the player about the new day's tasks. */
    char body[80];
    const quest_def_t *q0 = find_def((quest_id_t)p->quest_id[0]);
    const quest_def_t *q1 = find_def((quest_id_t)p->quest_id[1]);
    const quest_def_t *q2 = find_def((quest_id_t)p->quest_id[2]);
    snprintf(body, sizeof(body), "%s  %s  %s",
             q0 ? q0->short_name : "?",
             q1 ? q1->short_name : "?",
             q2 ? q2->short_name : "?");
    story_card_show("NEW DAY", body, NULL, 0xFFD166);
}

static void award_completion(pet_state_t *p, const quest_def_t *def)
{
    int new_happy = p->happy + 5;
    p->happy = (new_happy > 100) ? 100 : (uint8_t)new_happy;
    p->care_score += 5;
    audio_jingles_play(JINGLE_HAPPY);
    story_card_show(def->story_title, def->story_body, NULL, 0x06D6A0);
    ESP_LOGI(TAG, "quest %u completed", def->id);
}

static void bump_quest(pet_state_t *p, quest_id_t id, uint8_t amount)
{
    for (int i = 0; i < 3; i++) {
        if (p->quest_id[i] != id || p->quest_done[i]) continue;
        const quest_def_t *def = find_def(id);
        if (!def) return;
        int prog = p->quest_progress[i] + amount;
        if (prog > def->target) prog = def->target;
        p->quest_progress[i] = (uint8_t)prog;
        if (p->quest_progress[i] >= def->target) {
            p->quest_done[i] = 1;
            award_completion(p, def);
        }
        return;
    }
}

static void set_quest_min(pet_state_t *p, quest_id_t id, uint8_t value)
{
    for (int i = 0; i < 3; i++) {
        if (p->quest_id[i] != id || p->quest_done[i]) continue;
        const quest_def_t *def = find_def(id);
        if (!def) return;
        if (value <= p->quest_progress[i]) return;
        if (value > def->target) value = def->target;
        p->quest_progress[i] = value;
        if (p->quest_progress[i] >= def->target) {
            p->quest_done[i] = 1;
            award_completion(p, def);
        }
        return;
    }
}

void daily_quests_on_care(pet_state_t *p, care_action_t action)
{
    switch (action) {
    case CARE_FEED_MEAL:
    case CARE_FEED_SNACK:
        bump_quest(p, QUEST_FEED_3, 1); break;
    case CARE_PLAY:
        bump_quest(p, QUEST_PLAY_2, 1); break;
    case CARE_CLEAN_ONE:
        bump_quest(p, QUEST_CLEAN_1, 1); break;
    case CARE_DISCIPLINE:
        bump_quest(p, QUEST_DISCIPLINE_1, 1); break;
    default: break;
    }
}

void daily_quests_on_minigame(pet_state_t *p, int score)
{
    if (score < 0) return;
    bump_quest(p, QUEST_PLAY_2, 1);     /* a played round counts */
    set_quest_min(p, QUEST_MINIGAME_5, (uint8_t)((score > 255) ? 255 : score));
}

int daily_quests_status_line(const pet_state_t *p, char *out, size_t out_sz)
{
    if (out_sz == 0) return 0;
    out[0] = '\0';
    for (int i = 0; i < 3; i++) {
        if (p->quest_done[i]) continue;
        const quest_def_t *def = find_def((quest_id_t)p->quest_id[i]);
        if (!def) continue;
        return snprintf(out, out_sz, "today: %s %u/%u",
                        def->short_name, p->quest_progress[i], def->target);
    }
    return snprintf(out, out_sz, "all quests done");
}
