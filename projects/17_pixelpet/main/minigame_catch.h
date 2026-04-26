#pragma once

#include "lvgl.h"
#include "imu_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the minigame UI as a hidden child of `parent`. */
void minigame_catch_init(lv_obj_t *parent);

/** Begin a 30-second round. The game shows itself on top of any existing
 *  screen and steals the animation tick until it finishes. */
void minigame_catch_start(void);

/** Returns true between start() and the round's end. */
bool minigame_catch_is_active(void);

/** 30 Hz tick. Updates apple positions, checks collisions, ends the round
 *  when the timer expires. Returns the score for the just-ended round, or
 *  -1 if still running. */
int minigame_catch_tick(const imu_state_t *imu);

#ifdef __cplusplus
}
#endif
