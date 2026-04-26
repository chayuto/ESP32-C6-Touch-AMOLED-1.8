#pragma once

#include "lvgl.h"

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

void ui_screens_init(lv_obj_t *parent);
void ui_screens_show(screen_id_t id);
void ui_screens_next(void);
screen_id_t ui_screens_current(void);

#ifdef __cplusplus
}
#endif
