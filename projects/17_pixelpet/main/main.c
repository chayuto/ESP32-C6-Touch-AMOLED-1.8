/*
 * main.c — PixelPet (Phase 6: power management)
 *
 * Adds idle-driven brightness ramp, screen-off after long inactivity, and
 * a clean AXP2101 shutdown via the "Power off" sleep-screen button.
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
#include "imu_manager.h"
#include "minigame_catch.h"
#include "power_manager.h"
#include "asset_loader.h"
#include "fishbowl.h"
#include "idle_scheduler.h"
#include "intro_screens.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "driver/gpio.h"
#include <time.h>
#include <string.h>

static const char *TAG = "main";

static pet_state_t s_pet;
static bool s_imu_ok = false;

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
            if (raw) {
                power_manager_notify_activity();
                if (!intro_screens_is_visible()) {
                    ui_screens_next();
                }
            }
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
        /* Don't commit yet — onboarding is about to overwrite name +
         * species. The first commit lands when intro completes. */
    }
}

static void on_intro_done(species_id_t species, const char *name)
{
    s_pet.species_id = (uint8_t)species;
    strncpy(s_pet.name, name, PET_NAME_LEN);
    s_pet.name[PET_NAME_LEN] = '\0';
    s_pet.intro_done = true;
    pet_save_commit(&s_pet);
    ui_screens_apply_state(&s_pet);
    ESP_LOGI(TAG, "intro complete — pet \"%s\" species=%d ready",
             s_pet.name, s_pet.species_id);
    audio_jingles_play(JINGLE_HATCH);
}

/* ── LVGL timers ───────────────────────────────────────── */

static void apply_minigame_score(int score)
{
    int boost = score * 3;
    if (boost > 50) boost = 50;
    int new_happy = s_pet.happy + boost;
    s_pet.happy = (new_happy > 100) ? 100 : (uint8_t)new_happy;
    s_pet.energy = (s_pet.energy > 15) ? s_pet.energy - 15 : 0;
    s_pet.care_score += (uint32_t)score;
    audio_jingles_play(JINGLE_HAPPY);
    pet_save_request();
    ESP_LOGI(TAG, "minigame score %d → +%d happy", score, boost);
}

static void anim_timer_cb(lv_timer_t *t)
{
    if (minigame_catch_is_active()) {
        imu_state_t imu = {0};
        if (s_imu_ok) imu_manager_get_state(&imu);
        int score = minigame_catch_tick(&imu);
        if (score >= 0) apply_minigame_score(score);
    } else {
        fishbowl_tick();
        pet_renderer_tick();
        ui_screens_apply_state(&s_pet);
        idle_scheduler_update(&s_pet);
    }
}

static void stat_tick_cb(lv_timer_t *t)
{
    power_manager_tick();

    if (minigame_catch_is_active()) { pet_save_pump(&s_pet); return; }

    int64_t now = rtc_manager_now_unix();
    int64_t dt  = now - s_pet.last_update_unix;
    if (dt > 0) {
        s_pet.last_update_unix = now;
        stat_engine_decay(&s_pet, dt);
        stat_engine_check_transitions(&s_pet, now);
        pet_save_request();
    }
    pet_save_pump(&s_pet);

    /* Refresh day/night tint once per stat tick (1 Hz is plenty). */
    time_t tt = (time_t)now;
    struct tm tm_now;
    gmtime_r(&tt, &tm_now);
    fishbowl_apply_tint(tm_now.tm_hour);
}

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
    ESP_LOGI(TAG, "=== PixelPet (Phase 6) ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);
    esp_log_level_set("I2C_If", ESP_LOG_WARN);

    ESP_LOGI(TAG, "[1/10] NVS init");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_LOGI(TAG, "[2/10] BOOT button");
    boot_button_init();

    ESP_LOGI(TAG, "[3/10] AMOLED + I2C bus");
    ESP_ERROR_CHECK(amoled_init());

    ESP_LOGI(TAG, "[3.5/10] asset bundle");
    if (asset_loader_init() != ESP_OK) {
        ESP_LOGW(TAG, "asset bundle missing — flash with idf.py flash-assets");
    } else {
        ESP_LOGI(TAG, "asset bundle ready: %u entries", asset_count());
    }

    ESP_LOGI(TAG, "[4/10] PCF85063 RTC");
    if (rtc_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "RTC init failed — pet aging will be wrong");
    }

    ESP_LOGI(TAG, "[5/10] pet save load (or fresh egg)");
    pet_boot_load_or_create();

    ESP_LOGI(TAG, "[6/10] FT3168 touch");
    esp_lcd_touch_handle_t touch = NULL;
    if (amoled_touch_init(&touch) != ESP_OK) {
        ESP_LOGW(TAG, "touch init failed — UI input disabled");
        touch = NULL;
    }
    ESP_LOGI(TAG, "[7/10] LVGL");
    ESP_ERROR_CHECK(amoled_lvgl_init(amoled_get_panel(), touch));

    ESP_LOGI(TAG, "[8/10] audio (ES8311 + amp)");
    if (audio_output_init() == ESP_OK) {
        audio_output_amp_enable(true);
        audio_output_start_task();
    } else {
        ESP_LOGW(TAG, "audio not available — running silent");
    }

    ESP_LOGI(TAG, "[9/10] QMI8658 IMU");
    if (imu_manager_init() == ESP_OK) {
        imu_manager_start_task();
        s_imu_ok = true;
    } else {
        ESP_LOGW(TAG, "IMU not available — minigame will use neutral tilt");
    }

    ESP_LOGI(TAG, "[10/10] UI + power manager");
    ui_screens_init(lv_scr_act(), &s_pet);
    minigame_catch_init(lv_scr_act());
    intro_screens_init(lv_scr_act(), on_intro_done);
    ui_screens_apply_state(&s_pet);
    power_manager_init();
    idle_scheduler_init();

    if (!s_pet.intro_done) {
        ESP_LOGI(TAG, "first run — showing onboarding");
        intro_screens_show();
    }

    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 2, NULL);

    ESP_LOGI(TAG, "PixelPet running. stage=%s hunger=%d happy=%d energy=%d",
             pet_stage_name(s_pet.stage), s_pet.hunger, s_pet.happy, s_pet.energy);
    ESP_LOGI(TAG, "Free heap: %lu (min %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());
}
