/*
 * main.c — Sensory Play: 3-mode interactive toy for 15-24 month toddlers
 *
 * Modes: Snow Globe | Dance Party | Music Maker
 * Input: QMI8658 IMU (tilt/shake) + FT3168 touch + BOOT button (mode switch)
 * Output: SH8601 AMOLED (particles/animations) + ES8311 DAC + NS4150B speaker
 */

#include "amoled.h"
#include "amoled_touch.h"
#include "amoled_lvgl.h"
#include "imu_manager.h"
#include "audio_output.h"
#include "mode_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "driver/gpio.h"

static const char *TAG = "main";

/* ── BOOT Button (GPIO 9) — mode switch ────────────────── */

#define BOOT_BTN_GPIO  GPIO_NUM_9

static int s_btn_debounce = 0;
static bool s_btn_last_state = false;

static void boot_button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOOT_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

static void boot_button_poll(void)
{
    bool raw = (gpio_get_level(BOOT_BTN_GPIO) == 0);

    if (raw != s_btn_last_state) {
        s_btn_debounce++;
        if (s_btn_debounce >= 5) {
            s_btn_last_state = raw;
            s_btn_debounce = 0;
            if (raw) {
                /* Button pressed — switch mode */
                mode_manager_next();
            }
        }
    } else {
        s_btn_debounce = 0;
    }
}

/* ── Touch handling ────────────────────────────────────── */

static bool s_touch_was_pressed = false;

static void check_touch(esp_lcd_touch_handle_t tp)
{
    if (!tp) return;

    esp_lcd_touch_read_data(tp);

    uint16_t x, y;
    uint8_t count = 0;
    esp_lcd_touch_get_coordinates(tp, &x, &y, NULL, &count, 1);

    if (count > 0) {
        if (!s_touch_was_pressed) {
            mode_manager_on_touch((int16_t)x, (int16_t)y);
            s_touch_was_pressed = true;
        }
    } else {
        if (s_touch_was_pressed) {
            mode_manager_on_release();
            s_touch_was_pressed = false;
        }
    }
}

/* ── LVGL Timers ───────────────────────────────────────── */

static void anim_timer_cb(lv_timer_t *timer)
{
    imu_state_t imu;
    imu_manager_get_state(&imu);
    mode_manager_update(&imu);
}

/* ── LVGL Task ─────────────────────────────────────────── */

static esp_lcd_touch_handle_t s_touch = NULL;

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");

    /* Animation timer: 33ms ≈ 30 Hz */
    lv_timer_create(anim_timer_cb, 33, NULL);

    while (1) {
        boot_button_poll();

        /* Check touch directly (outside LVGL input, for mode_manager) */
        check_touch(s_touch);

        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

/* ── App Main ──────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Sensory Play ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    /* Suppress noisy logs */
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);
    esp_log_level_set("I2C_If", ESP_LOG_WARN);

    /* 1. NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* 2. BOOT button */
    boot_button_init();

    /* 3. Display + PMIC + IO Expander */
    ESP_ERROR_CHECK(amoled_init());

    /* 4. Touch */
    esp_err_t touch_ret = amoled_touch_init(&s_touch);
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (%s)", esp_err_to_name(touch_ret));
        s_touch = NULL;
    }

    /* 5. LVGL */
    ESP_ERROR_CHECK(amoled_lvgl_init(amoled_get_panel(), s_touch));

    /* 6. IMU */
    esp_err_t imu_ret = imu_manager_init();
    if (imu_ret != ESP_OK) {
        ESP_LOGW(TAG, "IMU init failed (%s) — continuing without motion", esp_err_to_name(imu_ret));
    }

    /* 7. Audio output */
    esp_err_t audio_ret = audio_output_init();
    if (audio_ret != ESP_OK) {
        ESP_LOGW(TAG, "Audio init failed (%s) — continuing without sound", esp_err_to_name(audio_ret));
    }

    /* 8. Speaker amp ON */
    audio_output_amp_enable(true);

    /* 9. Mode manager (creates all mode UIs) */
    mode_manager_init(lv_scr_act());

    /* 10. Start tasks */
    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);
    if (imu_ret == ESP_OK) {
        imu_manager_start_task();
    }
    if (audio_ret == ESP_OK) {
        audio_output_start_task();
    }

    /* 11. Brightness */
    amoled_set_brightness(180);

    ESP_LOGI(TAG, "Sensory Play running!");
    ESP_LOGI(TAG, "Free heap: %lu (min: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
