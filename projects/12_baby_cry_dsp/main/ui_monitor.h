#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create the baby monitor LVGL UI widgets.
 * Must be called after amoled_lvgl_init().
 */
void ui_monitor_init(void);

/**
 * Show "Connecting..." status on the display.
 */
void ui_monitor_show_connecting(void);

/**
 * Update the display with the connected IP address.
 */
void ui_monitor_show_ip(const char *ip);

/**
 * LVGL timer callback that polls detection state and updates UI.
 * Register this with lv_timer_create().
 */
void ui_monitor_timer_cb(lv_timer_t *timer);

/** Update button press count shown on display (call from any task) */
void ui_monitor_set_btn_count(int count, int brightness);

#ifdef __cplusplus
}
#endif
