#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create the UI on the given screen. */
void ui_init(lv_obj_t *screen);

/** Called periodically from LVGL task to update now-playing state. */
void ui_update(void);

/** Set child-lock state — disables touch on song list. */
void ui_set_child_lock(bool locked);

/** Get current child-lock state. */
bool ui_get_child_lock(void);

/** Turn screen on/off (for screen-off mode). */
void ui_set_screen_off(bool off);

/** Get screen-off state. */
bool ui_get_screen_off(void);

#ifdef __cplusplus
}
#endif
