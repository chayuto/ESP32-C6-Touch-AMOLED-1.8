#pragma once

#include "lvgl.h"
#include "pet_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SCREEN_STATUS = 0,
    SCREEN_FEED,
    SCREEN_PLAY,
    SCREEN_CLEAN,
    SCREEN_SLEEP,
    SCREEN_COUNT
} screen_id_t;

/** Build all screens. The pet pointer is retained for care-action callbacks. */
void ui_screens_init(lv_obj_t *parent, pet_state_t *pet);

void ui_screens_show(screen_id_t id);
void ui_screens_next(void);
screen_id_t ui_screens_current(void);

/** Push the current pet state into all screen widgets. */
void ui_screens_apply_state(const pet_state_t *p);

#ifdef __cplusplus
}
#endif
