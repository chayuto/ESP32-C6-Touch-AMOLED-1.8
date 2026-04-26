#pragma once

#include "lvgl.h"
#include "pet_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOOD_HAPPY = 0,
    MOOD_NEUTRAL,
    MOOD_SAD,
    MOOD_HUNGRY,
    MOOD_TIRED,
    MOOD_SICK,
    MOOD_PLAYING,
    MOOD_SLEEPING,
    MOOD_DEAD,
    MOOD_COUNT,
} pet_mood_t;

/** Build the pet sprite as a child of `parent`. Centre-aligned. */
void pet_renderer_init(lv_obj_t *parent);

/** Pull stage + mood from `p`, update pose, colours, accessory sprites (poop). */
void pet_renderer_set_state(const pet_state_t *p);

/** Advance idle animation by one step. Call from a ~30 Hz LVGL timer. */
void pet_renderer_tick(void);

/** Pure function: derive mood from stat thresholds. */
pet_mood_t pet_renderer_derive_mood(const pet_state_t *p);

#ifdef __cplusplus
}
#endif
