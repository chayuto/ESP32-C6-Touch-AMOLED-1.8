#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Build the white-noise view as a child of `parent`. Hidden by default. */
void ui_noise_build(lv_obj_t *parent);

/** Show / hide the noise view. The caller is responsible for hiding any
 *  other top-level view (song list, now-playing) before showing this one. */
void ui_noise_show(void);
void ui_noise_hide(void);
bool ui_noise_is_visible(void);

/** Invoked from ui_update() at ~10 Hz to refresh dynamic labels (countdown
 *  timer, volume readout, voice highlight, play/pause icon). */
void ui_noise_update(void);

#ifdef __cplusplus
}
#endif
