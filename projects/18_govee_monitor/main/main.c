/*
 * main.c — Govee Monitor: Govee H5075 BLE temperature/humidity tiles.
 *
 * Boots the AMOLED + LVGL stack, paints N tiles (N=CONFIG_GOVEE_MAX_SLOTS),
 * then starts a NimBLE passive scanner that decodes Govee H5075 advertisements
 * and routes them into the slot store. The LVGL refresh timer pulls a snapshot
 * every second and repaints.
 *
 * Pinned MACs (compile-time list in device_config.h) get fixed slots and
 * friendly labels. Remaining slots auto-fill by RSSI; if more than N H5075s
 * are in range, the strongest N win (with 6 dB hysteresis to prevent thrash).
 */

#include "amoled.h"
#include "amoled_touch.h"
#include "amoled_lvgl.h"

#include "ui.h"
#include "slot_store.h"
#include "ble_scanner.h"
#include "power_save.h"

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
    if (power_save_is_active()) return;     /* skip work while screen is off */
    slot_store_tick();
    ui_refresh();
}

static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task started");

    lv_timer_create(refresh_timer_cb, 1000, NULL);

    while (1) {
        power_save_poll();
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
    ui_create(lv_scr_act());

    power_save_init();

    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);

    amoled_set_brightness(180);

    /* BLE scanner runs its own NimBLE host task. */
    ESP_ERROR_CHECK(ble_scanner_start());

    ESP_LOGI(TAG, "Ready. Free heap: %lu (min: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
