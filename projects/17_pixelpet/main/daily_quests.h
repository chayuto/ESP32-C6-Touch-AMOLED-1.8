#pragma once

#include "pet_state.h"
#include "stat_engine.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Quest IDs are stable u8 codes — saved, so don't renumber. */
typedef enum {
    QUEST_NONE          = 0,
    QUEST_FEED_3        = 1,    /* feed 3 times today */
    QUEST_PLAY_2        = 2,    /* successful CARE_PLAY twice today */
    QUEST_CLEAN_1       = 3,    /* wipe at least one poop today */
    QUEST_DISCIPLINE_1  = 4,    /* scold once today */
    QUEST_MINIGAME_5    = 5,    /* score ≥5 in a single catch run */
    QUEST_COUNT
} quest_id_t;

/** Run any pending midnight reset (compares now vs last_quest_reset_unix
 *  and rolls a fresh slate if a new local day has started or no quests
 *  were ever assigned). Safe to call every stat tick — cheap. */
void daily_quests_check_reset(pet_state_t *p, int64_t now_unix);

/** Hook called after a successful CARE_* action. Increments any
 *  matching quest counter and fires a story-card on completion. */
void daily_quests_on_care(pet_state_t *p, care_action_t action);

/** Hook for minigame results — checks the score-target quest. */
void daily_quests_on_minigame(pet_state_t *p, int score);

/** Format a one-line "today: <quest> <progress>/<target>" string for
 *  the status screen. Returns the number of bytes written (≥ 0) or 0
 *  if no quests are pending. Always NUL-terminates. */
int daily_quests_status_line(const pet_state_t *p, char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif
