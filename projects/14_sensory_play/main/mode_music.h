#pragma once

#include "imu_manager.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void mode_music_init(lv_obj_t *parent);
void mode_music_enter(void);
void mode_music_update(const imu_state_t *imu);
void mode_music_on_touch(int16_t x, int16_t y);
void mode_music_on_release(void);
void mode_music_exit(void);

#ifdef __cplusplus
}
#endif
