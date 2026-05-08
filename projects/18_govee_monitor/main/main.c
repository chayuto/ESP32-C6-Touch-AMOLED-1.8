/*
 * main.c — Govee Monitor: 3× H5075 BLE temperature/humidity tiles.
 *
 * Phase 1: scaffold only. Boots the AMOLED + LVGL stack, paints a static
 * 3-tile UI from slot_store_load_placeholders() so we can verify display
 * and layout before any BLE work. NimBLE scanner lands in phase 2.
 */

#include "amoled.h"
#include "amoled_touch.h"
#include "amoled_lvgl.h"

#include "ui.h"
#include "slot_store.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "govee_mon";

static esp_lcd_touch_handle_t s_touch = NULL;

static void refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    slot_store_tick();
    ui_refresh();
}

static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task started");

    lv_timer_create(refresh_timer_cb, 1000, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Govee Monitor ===");
    ESP_LOGI(TAG, "Free heap: %lu", (unsigned long)esp_get_free_heap_size());

    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(amoled_init());

    if (amoled_touch_init(&s_touch) != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed — continuing without touch");
        s_touch = NULL;
    }

    ESP_ERROR_CHECK(amoled_lvgl_init(amoled_get_panel(), s_touch));

    slot_store_init();
    slot_store_load_placeholders();   /* phase 1: static demo data */

    ui_create(lv_scr_act());

    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);

    amoled_set_brightness(180);

    ESP_LOGI(TAG, "Ready. Free heap: %lu (min: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
