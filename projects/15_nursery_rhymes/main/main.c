/*
 * main.c — Nursery Rhyme Player for ESP32-C6-Touch-AMOLED-1.8
 *
 * A baby-friendly music player with 20 classic nursery rhymes.
 * Touch screen to browse and play songs. BOOT button controls:
 *   - Short press: toggle screen on/off (power saving, no screen time)
 *   - Long press (2s): toggle child lock (prevents touch interaction)
 *
 * Audio plays through ES8311 DAC + NS4150B speaker amplifier.
 * Songs continue playing even with screen off or child lock active.
 */

#include "audio_player.h"
#include "song_data.h"
#include "ui.h"
#include "amoled.h"
#include "amoled_lvgl.h"
#include "amoled_touch.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "main";

/* ── Boot Button (GPIO 9) ────────────────────────────── */

#define BOOT_BTN_GPIO       GPIO_NUM_9
#define LONG_PRESS_MS       2000
#define DEBOUNCE_COUNT      5      /* 5 × 10ms = 50ms debounce */

static bool    s_btn_pressed     = false;
static int     s_btn_debounce    = 0;
static int64_t s_btn_press_time  = 0;
static bool    s_btn_long_fired  = false;

static esp_lcd_touch_handle_t s_touch = NULL;

static void boot_button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOOT_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

static void boot_button_poll(void)
{
    bool raw = (gpio_get_level(BOOT_BTN_GPIO) == 0); /* active low */

    if (raw != s_btn_pressed) {
        s_btn_debounce++;
        if (s_btn_debounce >= DEBOUNCE_COUNT) {
            s_btn_pressed = raw;
            s_btn_debounce = 0;

            if (raw) {
                /* Button pressed down */
                s_btn_press_time = esp_timer_get_time();
                s_btn_long_fired = false;
            } else {
                /* Button released */
                if (!s_btn_long_fired) {
                    /* Short press → toggle screen on/off */
                    bool off = ui_get_screen_off();
                    ui_set_screen_off(!off);
                    ESP_LOGI(TAG, "Short press: screen %s", off ? "ON" : "OFF");
                }
            }
        }
    } else {
        s_btn_debounce = 0;
    }

    /* Check for long press while held */
    if (s_btn_pressed && !s_btn_long_fired) {
        int64_t held_ms = (esp_timer_get_time() - s_btn_press_time) / 1000;
        if (held_ms >= LONG_PRESS_MS) {
            s_btn_long_fired = true;
            bool locked = ui_get_child_lock();
            ui_set_child_lock(!locked);
            ESP_LOGI(TAG, "Long press: child lock %s", locked ? "OFF" : "ON");
        }
    }
}

/* ── LVGL Task ────────────────────────────────────────── */

static void lvgl_task(void *arg)
{
    /* Create a periodic timer for UI update at ~30Hz */
    lv_timer_t *ui_timer = lv_timer_create(
        (lv_timer_cb_t)ui_update, 100, NULL);
    (void)ui_timer;

    while (1) {
        boot_button_poll();
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}

/* ── App Main ─────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "=== Nursery Rhyme Player ===");
    ESP_LOGI(TAG, "%d songs loaded", g_song_count);

    /* 1. NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 2. Boot button */
    boot_button_init();

    /* 3. Display init (I2C → AXP2101 → TCA9554 → SH8601) */
    ESP_ERROR_CHECK(amoled_init());

    /* 4. Touch (FT3168) */
    ESP_ERROR_CHECK(amoled_touch_init(&s_touch));

    /* 5. LVGL (display + touch driver) */
    ESP_ERROR_CHECK(amoled_lvgl_init(amoled_get_panel(), s_touch));

    /* 6. Audio (I2S + ES8311) */
    ESP_ERROR_CHECK(audio_player_init());

    /* 7. Speaker amp ON */
    audio_player_amp_enable(true);

    /* 8. Build UI */
    ui_init(lv_scr_act());

    /* 9. Start tasks */
    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);
    audio_player_start_task();

    /* 10. Display brightness */
    amoled_set_brightness(180);

    ESP_LOGI(TAG, "Ready! Short-press BOOT=screen off, long-press=child lock");
}
