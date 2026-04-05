#pragma once

#include "imu_manager.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void mode_dance_init(lv_obj_t *parent);
void mode_dance_enter(void);
void mode_dance_update(const imu_state_t *imu);
void mode_dance_exit(void);

#ifdef __cplusplus
}
#endif
