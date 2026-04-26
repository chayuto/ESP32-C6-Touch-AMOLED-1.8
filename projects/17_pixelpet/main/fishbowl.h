#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the fishbowl backdrop on `parent` (drifting bubbles + tint).
 *  Should be called BEFORE pet_renderer_init so the pet draws on top. */
void fishbowl_init(lv_obj_t *parent);

/** Advance bubble positions one tick. Call from the 30 Hz LVGL timer. */
void fishbowl_tick(void);

/** Apply a tint based on the current hour (warm by day, cool at night).
 *  Call ~once a minute from the stat tick. */
void fishbowl_apply_tint(int hour_0_23);

#ifdef __cplusplus
}
#endif
