/*
 * main.c — PixelPet (Phase 1: scaffolding only)
 *
 * Brings up display + touch + LVGL, shows a "Hello PixelPet" placeholder,
 * and lets BOOT (GPIO 9) cycle through five placeholder screens.
 * Pet state, sprites, audio, IMU, and persistence land in later phases.
 */

#include "amoled.h"
#include "amoled_touch.h"
#include "amoled_lvgl.h"
#include "ui_screens.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "driver/gpio.h"

static const char *TAG = "main";

/* ── BOOT Button (GPIO 9) — screen cycle ──────────────── */

#define BOOT_BTN_GPIO  GPIO_NUM_9

static int  s_btn_debounce = 0;
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
                ui_screens_next();
            }
        }
    } else {
        s_btn_debounce = 0;
    }
}

/* ── LVGL task ─────────────────────────────────────────── */

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    while (1) {
        boot_button_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

/* ── App main ──────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== PixelPet (Phase 1 scaffold) ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

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

    /* 3. Display + PMIC + IO expander */
    ESP_ERROR_CHECK(amoled_init());

    /* 4. Touch */
    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_ret = amoled_touch_init(&touch);
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (%s)", esp_err_to_name(touch_ret));
        touch = NULL;
    }

    /* 5. LVGL */
    ESP_ERROR_CHECK(amoled_lvgl_init(amoled_get_panel(), touch));

    /* 6. UI screens */
    ui_screens_init(lv_scr_act());

    /* 7. LVGL task */
    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);

    /* 8. Brightness */
    amoled_set_brightness(150);

    ESP_LOGI(TAG, "PixelPet running — press BOOT to cycle screens");
    ESP_LOGI(TAG, "Free heap: %lu (min %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
