#pragma once

#include "pet_renderer.h"
#include "pet_state.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Single-clock big-idle picker. The renderer's body anim already covers
 * the "breath" channel via uneven frame holds, so the scheduler ships
 * with one timer (big-idle). Blink + glance are reserved for a future
 * commit that adds eye-overlay sprites. */
void idle_scheduler_init(void);

/* Push the latest pet state. Mood drives tempo; sleeping/egg/dead
 * silence the picker entirely. Cheap to call every UI tick. */
void idle_scheduler_update(const pet_state_t *p);

/* Force the scheduler to skip its current wait. Useful when the player
 * has just interacted with the pet and the next big idle should land
 * sooner. Called from touch and care callbacks. */
void idle_scheduler_pause(bool paused);

#ifdef __cplusplus
}
#endif
