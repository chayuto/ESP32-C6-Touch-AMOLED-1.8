#pragma once

#include "imu_manager.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void mode_snow_globe_init(lv_obj_t *parent);
void mode_snow_globe_enter(void);
void mode_snow_globe_update(const imu_state_t *imu);
void mode_snow_globe_exit(void);

#ifdef __cplusplus
}
#endif
