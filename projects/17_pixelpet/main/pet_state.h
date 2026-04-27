#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STAGE_EGG = 0,
    STAGE_BABY,
    STAGE_CHILD,
    STAGE_TEEN,
    STAGE_ADULT,
    STAGE_SENIOR,
    STAGE_DEAD,
    STAGE_COUNT,
} pet_stage_t;

typedef enum {
    ADULT_HAPPY = 0,
    ADULT_LAZY,
    ADULT_NAUGHTY,
    ADULT_SICK,
} pet_adult_form_t;

typedef struct {
    int64_t  hatched_unix;
    int64_t  last_update_unix;
    pet_stage_t stage;
    pet_adult_form_t adult_form;

    uint8_t  hunger;     /* 0-100 (100 = full) */
    uint8_t  happy;      /* 0-100 */
    uint8_t  energy;     /* 0-100 */
    uint8_t  clean;      /* 0-100 */
    uint8_t  disc;       /* 0-100 */
    uint8_t  health;     /* 0-100 */

    uint32_t care_score;
    uint8_t  poop_count; /* 0-3 */
    bool     is_sleeping;

    /* v2 fields below — append-only so old saves migrate cleanly. */
    uint8_t  species_id;

    /* v3 fields. name[] is a 3-char display name + NUL; intro_done gates
     * the first-run onboarding flow. */
    char     name[8];
    bool     intro_done;

    /* v4 fields — lifetime milestone counters used by the memorial card
     * and milestone story beats. Append-only. */
    uint32_t total_meals;       /* CARE_FEED_MEAL + CARE_FEED_SNACK */
    uint32_t total_plays;       /* CARE_PLAY + completed minigames */
    uint32_t total_cleans;      /* CARE_CLEAN_ONE */
    uint32_t total_meds;        /* CARE_MEDICINE */
    uint32_t minigame_high;     /* best catch score */

    /* v5 fields — three-slot daily quest state, reset at RTC midnight.
     * quest_id values come from daily_quests.h (uint8 = QUEST_*). */
    uint8_t  quest_id[3];
    uint8_t  quest_progress[3];
    uint8_t  quest_done[3];     /* 0/1; redundant with progress >= target */
    int64_t  last_quest_reset_unix;
} pet_state_t;

#define PET_NAME_LEN  3   /* characters used in name[]; name[3] is NUL */

/** Initialise a fresh egg with full stats and unset timestamps (caller fills in). */
void pet_state_init_new(pet_state_t *p);

const char *pet_stage_name(pet_stage_t s);

#ifdef __cplusplus
}
#endif
