#pragma once

#include "imu_manager.h"
#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MODE_SNOW_GLOBE = 0,
    MODE_DANCE_PARTY,
    MODE_MUSIC_MAKER,
    MODE_COUNT,
} play_mode_t;

/** Initialize mode manager — creates UI containers for each mode */
void mode_manager_init(lv_obj_t *screen);

/** Switch to next mode (called from button handler) */
void mode_manager_next(void);

/** Get current active mode */
play_mode_t mode_manager_current(void);

/** Called from LVGL animation timer (~30 Hz). Updates active mode. */
void mode_manager_update(const imu_state_t *imu);

/** Called when touch event occurs */
void mode_manager_on_touch(int16_t x, int16_t y);

/** Called when touch is released */
void mode_manager_on_release(void);

#ifdef __cplusplus
}
#endif
