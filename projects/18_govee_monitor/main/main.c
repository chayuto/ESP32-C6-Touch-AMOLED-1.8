/*
 * main.c — Govee Monitor: Govee H5075 BLE temperature/humidity tiles.
 *
 * Boots the AMOLED + LVGL stack, paints N tiles (N=CONFIG_GOVEE_MAX_SLOTS),
 * then starts a NimBLE passive scanner that decodes Govee H5075 advertisements
 * and routes them into the slot store. The LVGL refresh timer pulls a snapshot
 * every second and repaints.
 *
 * Every decoded reading is also folded into a rolling in-RAM history (1 h /
 * 6 h / 24 h tiers) on its own esp_timer, deliberately independent of the LVGL
 * refresh timer so collection continues while the screen is asleep.
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
#include "history.h"
#include "net_time.h"
#include "uploader.h"
#include "ble_scanner.h"
#include "ble_scanner.h"
#include "power_save.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_timer.h"
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

/* One line every 30 s summarising everything a human would ask about. Silence
 * must never be the steady state: without this a WiFi drop, a clock that never
 * synced or a sensor that went quiet are all invisible until someone plots the
 * data days later and finds a hole. */
static void heartbeat_timer_cb(void *arg)
{
    (void)arg;

    slot_t snap[SLOT_STORE_MAX];
    slot_store_snapshot(snap);

    int live = 0, stale = 0;
    for (int i = 0; i < CONFIG_GOVEE_MAX_SLOTS; i++) {
        if (!snap[i].valid) continue;
        if (snap[i].stale) stale++; else live++;
    }

    static const char *SRC[] = { "NONE", "RTC", "NTP" };
    int64_t sync_age = net_time_since_sync_s();

    ESP_LOGI(TAG,
             "status: wifi=%s clock=%s sync_age=%llds "
             "slots=%d/%d/%d(live/stale/max) adverts=%lu screen=%s "
             "up[sent=%lu dup=%lu fail=%lu backlog=%u age=%llds] heap=%lu min=%lu",
             net_time_wifi_up() ? "up" : "down",
             SRC[net_time_clock_src()],
             (long long)sync_age,
             live, stale, CONFIG_GOVEE_MAX_SLOTS,
             (unsigned long)ble_scanner_advert_count(),
             power_save_is_active() ? "off" : "on",
             (unsigned long)uploader_rows_sent(),
             (unsigned long)uploader_rows_duplicate(),
             (unsigned long)uploader_failures(),
             uploader_backlog(),
             (long long)uploader_since_success_s(),
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}

/* History must keep bucketing while the display is off, so it runs on its own
 * timer rather than piggybacking on refresh_timer_cb (which returns early
 * during power save). */
static void history_timer_cb(void *arg)
{
    (void)arg;
    history_tick();
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
    history_init();

    /* Non-blocking: brings up the RTC now, WiFi and NTP whenever they become
     * available. The monitor is fully functional if neither ever does. */
    net_time_init();
    uploader_start();
    ui_create(lv_scr_act());

    const esp_timer_create_args_t history_timer_args = {
        .callback = history_timer_cb,
        .name     = "history",
    };
    esp_timer_handle_t history_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&history_timer_args, &history_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(history_timer, 1000000));  /* 1 s */

    const esp_timer_create_args_t heartbeat_timer_args = {
        .callback = heartbeat_timer_cb,
        .name     = "heartbeat",
    };
    esp_timer_handle_t heartbeat_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&heartbeat_timer_args, &heartbeat_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(heartbeat_timer, 30000000));  /* 30 s */

    power_save_init();

    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);

    amoled_set_brightness(180);

    /* BLE scanner runs its own NimBLE host task. */
    ESP_ERROR_CHECK(ble_scanner_start());

    ESP_LOGI(TAG, "Ready. Free heap: %lu (min: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
