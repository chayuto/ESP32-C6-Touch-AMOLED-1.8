/*
 * main.c — PixelPet (Phase 3: stat engine + care actions + jingles)
 *
 * Runs the time-based decay engine, lets the player feed/play/clean/sleep
 * the pet via the touch screens, and plays chiptune jingles for events.
 * Real RTC-based persistence and IMU mini-games arrive in later phases.
 */

#include "amoled.h"
#include "amoled_touch.h"
#include "amoled_lvgl.h"
#include "ui_screens.h"
#include "pet_renderer.h"
#include "pet_state.h"
#include "stat_engine.h"
#include "audio_output.h"
#include "audio_jingles.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "driver/gpio.h"

static const char *TAG = "main";

static pet_state_t s_pet;

/* ── BOOT button ───────────────────────────────────────── */

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
            if (raw) ui_screens_next();
        }
    } else {
        s_btn_debounce = 0;
    }
}

/* ── LVGL timers ───────────────────────────────────────── */

static void anim_timer_cb(lv_timer_t *t)
{
    pet_renderer_tick();
    ui_screens_apply_state(&s_pet);
}

static void stat_tick_cb(lv_timer_t *t)
{
    int64_t now_us = esp_timer_get_time();
    int64_t dt_us  = now_us - s_pet.last_update_unix;
    s_pet.last_update_unix = now_us;

    stat_engine_decay(&s_pet, dt_us);
    stat_engine_check_transitions(&s_pet, now_us);
}

/* ── LVGL task ─────────────────────────────────────────── */

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    lv_timer_create(anim_timer_cb,  33,   NULL);  /* 30 Hz animation */
    lv_timer_create(stat_tick_cb,   1000, NULL);  /* 1 Hz decay      */

    while (1) {
        boot_button_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

/* ── App main ──────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== PixelPet (Phase 3) ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);
    esp_log_level_set("I2C_If", ESP_LOG_WARN);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    boot_button_init();

    ESP_ERROR_CHECK(amoled_init());

    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_ret = amoled_touch_init(&touch);
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (%s)", esp_err_to_name(touch_ret));
        touch = NULL;
    }

    ESP_ERROR_CHECK(amoled_lvgl_init(amoled_get_panel(), touch));

    /* Audio is best-effort; if init fails, jingles silently no-op. */
    esp_err_t audio_ret = audio_output_init();
    if (audio_ret == ESP_OK) {
        audio_output_amp_enable(true);
        audio_output_start_task();
    } else {
        ESP_LOGW(TAG, "Audio init failed (%s) — running silent", esp_err_to_name(audio_ret));
    }

    pet_state_init_new(&s_pet);
    s_pet.hatched_unix     = esp_timer_get_time();
    s_pet.last_update_unix = s_pet.hatched_unix;

    ui_screens_init(lv_scr_act(), &s_pet);
    ui_screens_apply_state(&s_pet);

    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);

    amoled_set_brightness(150);

    ESP_LOGI(TAG, "PixelPet running. Free heap: %lu (min %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
