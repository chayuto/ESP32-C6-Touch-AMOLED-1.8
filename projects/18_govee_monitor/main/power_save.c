#include "power_save.h"
#include "ble_scanner.h"

#include "amoled.h"
#include "amoled_lvgl.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "power";

#define BOOT_BTN_GPIO       GPIO_NUM_9
#define DEBOUNCE_TICKS      5             /* ~50 ms at 10 ms poll */
#define DEBOUNCE_REPEAT_MS  300           /* ignore repeat presses while held */

static bool s_active = false;
static int  s_debounce = 0;
static bool s_last = false;
static int64_t s_last_toggle_us = 0;

void power_save_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOOT_BTN_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    s_active = false;
}

bool power_save_is_active(void)
{
    return s_active;
}

void power_save_toggle(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_toggle_us < (int64_t)DEBOUNCE_REPEAT_MS * 1000) {
        return;
    }
    s_last_toggle_us = now;

    if (!s_active) {
        ESP_LOGI(TAG, "→ power save (display off, BLE pause)");
        ble_scanner_pause();
        amoled_lvgl_set_touch_enabled(false);
        amoled_set_brightness(0);
        amoled_display_on_off(false);
        s_active = true;
    } else {
        ESP_LOGI(TAG, "→ resume (display on, BLE scan)");
        amoled_display_on_off(true);
        amoled_set_brightness(180);
        amoled_lvgl_set_touch_enabled(true);
        ble_scanner_resume();
        s_active = false;
    }
}

void power_save_poll(void)
{
    bool pressed = (gpio_get_level(BOOT_BTN_GPIO) == 0);

    if (pressed != s_last) {
        s_debounce++;
        if (s_debounce >= DEBOUNCE_TICKS) {
            s_last = pressed;
            s_debounce = 0;
            if (pressed) {
                power_save_toggle();
            }
        }
    } else {
        s_debounce = 0;
    }
}
