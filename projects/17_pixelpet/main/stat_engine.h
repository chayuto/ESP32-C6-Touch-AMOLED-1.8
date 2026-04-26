#pragma once

#include "pet_state.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CARE_FEED_MEAL = 0,
    CARE_FEED_SNACK,
    CARE_PLAY,
    CARE_CLEAN_ONE,
    CARE_SLEEP_TOGGLE,
    CARE_DISCIPLINE,
    CARE_MEDICINE,
    CARE_COUNT,
} care_action_t;

/** Apply elapsed-time decay to all stats. dt_us = microseconds since last call. */
void stat_engine_decay(pet_state_t *p, int64_t dt_us);

/** Apply a single care action. Returns true if action had an effect. */
bool stat_engine_apply_care(pet_state_t *p, care_action_t action);

/** Re-evaluate stage based on age and care_score. Plays JINGLE_LEVELUP on transition. */
void stat_engine_check_transitions(pet_state_t *p, int64_t now_us);

const char *care_action_name(care_action_t a);

#ifdef __cplusplus
}
#endif
