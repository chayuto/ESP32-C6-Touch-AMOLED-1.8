/*
 * main.c — PixelPet (Phase 2: pet state + procedural renderer + stat bars)
 *
 * Brings up display + touch + LVGL, instantiates a fresh egg, animates the
 * procedural pet, and drops stats slowly so we can see the bars move.
 * Real RTC, NVS persistence, audio, and IMU land in later phases.
 */

#include "amoled.h"
#include "amoled_touch.h"
#include "amoled_lvgl.h"
#include "ui_screens.h"
#include "pet_renderer.h"
#include "pet_state.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "driver/gpio.h"

static const char *TAG = "main";

/* Phase 2 holds pet state in RAM only; Phase 4 will load/save NVS. */
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

/* Debug stat tick — Phase 3 will replace with proper time-aware decay. */
static void debug_stat_tick_cb(lv_timer_t *t)
{
    if (s_pet.hunger > 0) s_pet.hunger--;
    if (s_pet.happy  > 0 && (esp_timer_get_time() / 1000000) % 2 == 0) s_pet.happy--;
    if (s_pet.energy > 0 && (esp_timer_get_time() / 1000000) % 3 == 0) s_pet.energy--;

    /* Auto-hatch after 10s for Phase 2 demo */
    if (s_pet.stage == STAGE_EGG) {
        int64_t age_s = (esp_timer_get_time() - s_pet.hatched_unix) / 1000000;
        if (age_s > 10) s_pet.stage = STAGE_BABY;
    }
}

/* ── LVGL task ─────────────────────────────────────────── */

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    lv_timer_create(anim_timer_cb,        33,   NULL);  /* ~30 Hz */
    lv_timer_create(debug_stat_tick_cb,   1000, NULL);  /* 1 Hz   */

    while (1) {
        boot_button_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

/* ── App main ──────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== PixelPet (Phase 2) ===");
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

    /* Initialise pet — fresh egg, in-memory only for Phase 2 */
    pet_state_init_new(&s_pet);
    s_pet.hatched_unix     = esp_timer_get_time();
    s_pet.last_update_unix = s_pet.hatched_unix;

    ui_screens_init(lv_scr_act());
    ui_screens_apply_state(&s_pet);

    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);

    amoled_set_brightness(150);

    ESP_LOGI(TAG, "PixelPet running — egg will hatch in 10s");
    ESP_LOGI(TAG, "Free heap: %lu (min %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
