/*
 * power_manager.c — idle dim, display-off, amp-off, AXP2101 power-off.
 *
 * LVGL already tracks "inactive time" (ms since last touch/keypad input)
 * via lv_disp_get_inactive_time(). We hook into that and add IMU shake
 * detection for free wake-ups when the device is picked up.
 *
 * Deep-sleep wake on the BOOT button is awkward on the C6 (GPIO 9 is not
 * an LP I/O), so the "off" state we expose is amoled_power_off() — a clean
 * AXP2101 shutdown that needs the PWR button to come back. Time is
 * preserved by the PCF85063 + backup battery, so the resume fast-forward
 * still works.
 */

#include "power_manager.h"
#include "amoled.h"
#include "audio_output.h"
#include "imu_manager.h"
#include "pet_save.h"
#include "esp_log.h"
#include "lvgl.h"

#define DIM_AT_MS         30000     /* 30 s → brightness 80 */
#define LOW_AT_MS         60000     /* 60 s → brightness 30 */
#define SCREEN_OFF_AT_MS  120000    /* 2 min → display + amp off */

#define BRIGHT_FULL  150
#define BRIGHT_DIM   80
#define BRIGHT_LOW   30

static const char *TAG = "power";

static bool s_screen_off = false;
static int  s_last_brightness = -1;

static void set_brightness(uint8_t level)
{
    if (s_last_brightness == level) return;
    amoled_set_brightness(level);
    s_last_brightness = level;
}

static void wake_from_idle(void)
{
    if (s_screen_off) {
        amoled_display_on_off(true);
        audio_output_amp_enable(true);
        s_screen_off = false;
        ESP_LOGI(TAG, "wake → display on");
    }
    set_brightness(BRIGHT_FULL);
    /* lv_disp_get_inactive_time(NULL) does not have a public reset on this
     * LVGL version; we trigger an internal reset by reporting a synthetic
     * input. The cheapest is calling lv_disp_trig_activity. */
    lv_disp_trig_activity(NULL);
}

void power_manager_init(void)
{
    s_screen_off = false;
    s_last_brightness = -1;
    set_brightness(BRIGHT_FULL);
}

void power_manager_notify_activity(void)
{
    wake_from_idle();
}

void power_manager_tick(void)
{
    /* Treat IMU shake as activity. */
    imu_state_t imu = {0};
    imu_manager_get_state(&imu);
    if (imu.is_shaking) {
        wake_from_idle();
        return;
    }

    uint32_t idle_ms = lv_disp_get_inactive_time(NULL);

    if (idle_ms < DIM_AT_MS) {
        if (s_screen_off) wake_from_idle();
        else              set_brightness(BRIGHT_FULL);
    } else if (idle_ms < LOW_AT_MS) {
        if (s_screen_off) wake_from_idle();
        else              set_brightness(BRIGHT_DIM);
    } else if (idle_ms < SCREEN_OFF_AT_MS) {
        if (!s_screen_off) set_brightness(BRIGHT_LOW);
    } else {
        if (!s_screen_off) {
            ESP_LOGI(TAG, "idle %lums → screen off", (unsigned long)idle_ms);
            audio_output_amp_enable(false);
            amoled_display_on_off(false);
            s_screen_off = true;
        }
    }
}

bool power_manager_is_screen_off(void) { return s_screen_off; }

void power_manager_power_off(void)
{
    ESP_LOGW(TAG, "power off requested");
    /* Save handled by caller before invoking us. The AXP shutdown cuts
     * everything including the I2C bus, so the call returns when the rail
     * collapses. */
    amoled_power_off();
}
