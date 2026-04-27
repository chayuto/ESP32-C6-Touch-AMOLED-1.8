#pragma once

#include "lvgl.h"
#include "pet_state.h"
#include "species.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Called when the user finishes onboarding. The intro module hides
 *  itself before this fires. The name buffer is at least PET_NAME_LEN+1
 *  bytes and NUL-terminated. */
typedef void (*intro_done_cb_t)(species_id_t species, const char *name);

/** Build the intro overlay widgets as children of `parent`. Hidden by
 *  default; call intro_screens_show() to bring them up. */
void intro_screens_init(lv_obj_t *parent, intro_done_cb_t on_done);

/** Reveal the welcome card and start the onboarding flow. */
void intro_screens_show(void);

/** Hide the entire onboarding overlay. */
void intro_screens_hide(void);

/** True while any onboarding card is visible. */
bool intro_screens_is_visible(void);

#ifdef __cplusplus
}
#endif
