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

/** Play a body anim once, then restore the mood-baseline anim. The name
 *  is a logical anim name like "yawn" or "eat" — the species + palette
 *  suffix is added internally. Returns true if the asset was found and
 *  the playback started. A no-op (and false) when the pet is in egg/dead
 *  stage or the asset is missing. */
bool pet_renderer_play_oneshot_anim(const char *anim_name);

/** R8 — feed reaction. Plays the eat body anim once. */
void pet_renderer_play_eat(void);

/** R8 — stage transition celebration. Sparkle burst overlay centred on
 *  the pet plus a one-shot of the happy body anim. Sprite zoom is not
 *  available on this board (LVGL 8.4 clipping trap), so the original
 *  plan's "scale tween" is replaced with the happy bounce. */
void pet_renderer_play_stageup(void);

/** R8 — react to a screen-space touch. Pops an "exclaim" particle near
 *  (x,y) for a short window and triggers a one-shot of the happy anim.
 *  x/y are absolute screen coordinates from the LVGL pointer event. */
void pet_renderer_react_to_touch(int x, int y);

#ifdef __cplusplus
}
#endif
