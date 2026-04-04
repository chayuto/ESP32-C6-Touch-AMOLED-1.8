#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Callback invoked during each LVGL flush. Used by snapshot to capture pixels.
 */
typedef void (*amoled_flush_hook_t)(const lv_area_t *area, lv_color_t *color_map);

/**
 * Initialize LVGL: lv_init, draw buffers (DMA), display driver with rounder,
 * tick timer, and touch input device.
 *
 * @param panel  Panel handle from amoled_init()
 * @param touch  Touch handle from amoled_touch_init() (NULL to skip touch)
 */
esp_err_t amoled_lvgl_init(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t touch);

/**
 * Get the registered LVGL display.
 */
lv_disp_t *amoled_lvgl_get_disp(void);

/**
 * Set a hook that is called during every LVGL flush with the area and pixel data.
 * Set to NULL to disable. Used by snapshot module for flush-callback capture.
 */
void amoled_lvgl_set_flush_hook(amoled_flush_hook_t hook);

#ifdef __cplusplus
}
#endif
