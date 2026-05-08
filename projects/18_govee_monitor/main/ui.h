#pragma once

#include "lvgl.h"

void ui_create(lv_obj_t *parent);

/* Called from the LVGL refresh timer to repaint tiles from slot snapshot. */
void ui_refresh(void);
