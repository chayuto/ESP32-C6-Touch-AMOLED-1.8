/*
 * main.c — PixelPet (Phase 4: NVS save + PCF85063 RTC + offline aging)
 *
 * Boot sequence loads the saved pet, asks the RTC for the current wall
 * clock, computes the time elapsed since `last_update_unix`, and runs the
 * decay engine forward by exactly that delta — so a pet left switched off
 * for hours wakes up genuinely older and hungrier.
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
#include "rtc_manager.h"
#include "pet_save.h"

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

/* ── Pet boot flow ─────────────────────────────────────── */

static void pet_boot_load_or_create(void)
{
    int64_t now = rtc_manager_now_unix();

    esp_err_t err = pet_save_load(&s_pet);
    if (err == ESP_OK) {
        int64_t dt = now - s_pet.last_update_unix;
        if (dt < 0) dt = 0;
        if (dt > 0) {
            ESP_LOGI(TAG, "fast-forwarding %lld real-seconds of decay", dt);
            stat_engine_decay(&s_pet, dt);
            stat_engine_check_transitions(&s_pet, now);
        }
        s_pet.last_update_unix = now;
        pet_save_commit(&s_pet);
    } else {
        ESP_LOGI(TAG, "no save found — hatching a fresh egg");
        pet_state_init_new(&s_pet);
        s_pet.hatched_unix     = now;
        s_pet.last_update_unix = now;
        pet_save_commit(&s_pet);
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
    int64_t now = rtc_manager_now_unix();
    int64_t dt  = now - s_pet.last_update_unix;
    if (dt > 0) {
        s_pet.last_update_unix = now;
        stat_engine_decay(&s_pet, dt);
        stat_engine_check_transitions(&s_pet, now);
        pet_save_request();
    }
    pet_save_pump(&s_pet);
}

/* ── LVGL task ─────────────────────────────────────────── */

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    lv_timer_create(anim_timer_cb,  33,   NULL);
    lv_timer_create(stat_tick_cb,   1000, NULL);

    while (1) {
        boot_button_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

/* ── App main ──────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== PixelPet (Phase 4) ===");
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

    /* 3. Display + I2C bus */
    ESP_ERROR_CHECK(amoled_init());

    /* 4. RTC (must be after amoled_init for the I2C bus) */
    esp_err_t rtc_ret = rtc_manager_init();
    if (rtc_ret != ESP_OK) {
        ESP_LOGE(TAG, "RTC init failed: %s — pet aging will be wrong",
                 esp_err_to_name(rtc_ret));
    }

    /* 5. Load or create pet — runs decay across the missing time */
    pet_boot_load_or_create();

    /* 6. Touch + LVGL */
    esp_lcd_touch_handle_t touch = NULL;
    esp_err_t touch_ret = amoled_touch_init(&touch);
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (%s)", esp_err_to_name(touch_ret));
        touch = NULL;
    }
    ESP_ERROR_CHECK(amoled_lvgl_init(amoled_get_panel(), touch));

    /* 7. Audio (best-effort) */
    if (audio_output_init() == ESP_OK) {
        audio_output_amp_enable(true);
        audio_output_start_task();
    } else {
        ESP_LOGW(TAG, "audio not available — running silent");
    }

    /* 8. UI */
    ui_screens_init(lv_scr_act(), &s_pet);
    ui_screens_apply_state(&s_pet);

    /* 9. LVGL task */
    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);

    /* 10. Brightness */
    amoled_set_brightness(150);

    ESP_LOGI(TAG, "PixelPet running. Free heap: %lu (min %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
