#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create the LVGL telemetry dashboard screen.
 */
void ui_dashboard_init(void);

/**
 * LVGL timer callback — polls telemetry and updates labels.
 * Register with lv_timer_create(ui_dashboard_timer_cb, 500, NULL).
 */
void ui_dashboard_timer_cb(lv_timer_t *timer);

/**
 * Show SD export status message briefly.
 */
void ui_dashboard_show_sd_status(const char *msg);

#ifdef __cplusplus
}
#endif
