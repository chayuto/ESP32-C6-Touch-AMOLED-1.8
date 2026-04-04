#pragma once

#include "lvgl.h"

#define MAX_OBJECTS 128

/**
 * Initialize the drawing screen and register the 50ms render timer.
 * Must be called after amoled_lvgl_init() and drawing_engine_init().
 */
void ui_display_init(void);

/** Queue a "Connecting to Wi-Fi..." screen via draw commands. */
void ui_display_show_connecting(void);

/** Queue an "IP ready" screen showing the IP and MCP endpoint. */
void ui_display_show_ip(const char *ip_str);

/** LVGL timer callback — drains draw queue, creates widgets. Registered in ui_display_init(). */
void ui_render_timer_cb(lv_timer_t *timer);

/** Current number of drawing objects on screen. */
int ui_display_get_obj_count(void);
