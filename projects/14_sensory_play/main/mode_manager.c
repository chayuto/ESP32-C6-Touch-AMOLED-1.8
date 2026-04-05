/*
 * mode_manager.c — Mode switching, session timer, transition display
 *
 * BOOT button cycles through Snow Globe → Dance Party → Music Maker.
 * Each mode runs for up to 3 minutes before returning to idle.
 */

#include "mode_manager.h"
#include "mode_snow_globe.h"
#include "mode_dance.h"
#include "mode_music.h"
#include "particle_engine.h"
#include "amoled.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "mode_mgr";

static play_mode_t s_current = MODE_SNOW_GLOBE;
static lv_obj_t *s_mode_containers[MODE_COUNT] = {NULL};
static lv_obj_t *s_transition_label = NULL;
static int64_t s_session_start_us = 0;
static int64_t s_transition_end_us = 0;
static bool s_in_transition = false;

#define SESSION_DURATION_US  (3 * 60 * 1000000LL)  /* 3 minutes */
#define TRANSITION_SHOW_US   (1000000LL)           /* 1 second */

static const char *s_mode_names[MODE_COUNT] = {
    "Snow Globe",
    "Dance Party",
    "Music Maker",
};

void mode_manager_init(lv_obj_t *screen)
{
    /* Create a container for each mode (full-screen, stacked) */
    for (int i = 0; i < MODE_COUNT; i++) {
        s_mode_containers[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(s_mode_containers[i]);
        lv_obj_set_size(s_mode_containers[i], AMOLED_LCD_H_RES, AMOLED_LCD_V_RES);
        lv_obj_set_pos(s_mode_containers[i], 0, 0);
        lv_obj_clear_flag(s_mode_containers[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_mode_containers[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Initialize each mode's UI */
    mode_snow_globe_init(s_mode_containers[MODE_SNOW_GLOBE]);
    mode_dance_init(s_mode_containers[MODE_DANCE_PARTY]);
    mode_music_init(s_mode_containers[MODE_MUSIC_MAKER]);

    /* Transition overlay label */
    s_transition_label = lv_label_create(screen);
    lv_obj_set_style_text_color(s_transition_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_transition_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_bg_color(s_transition_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_transition_label, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_transition_label, 20, 0);
    lv_obj_add_flag(s_transition_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(s_transition_label, LV_ALIGN_CENTER, 0, 0);

    /* Start with first mode */
    lv_obj_clear_flag(s_mode_containers[s_current], LV_OBJ_FLAG_HIDDEN);
    mode_snow_globe_enter();
    s_session_start_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Mode manager initialized, starting with %s", s_mode_names[s_current]);
}

static void enter_mode(play_mode_t mode)
{
    switch (mode) {
    case MODE_SNOW_GLOBE:  mode_snow_globe_enter(); break;
    case MODE_DANCE_PARTY: mode_dance_enter(); break;
    case MODE_MUSIC_MAKER: mode_music_enter(); break;
    default: break;
    }
}

static void exit_mode(play_mode_t mode)
{
    switch (mode) {
    case MODE_SNOW_GLOBE:  mode_snow_globe_exit(); break;
    case MODE_DANCE_PARTY: mode_dance_exit(); break;
    case MODE_MUSIC_MAKER: mode_music_exit(); break;
    default: break;
    }
}

void mode_manager_next(void)
{
    int64_t now = esp_timer_get_time();

    /* Exit current mode */
    exit_mode(s_current);
    lv_obj_add_flag(s_mode_containers[s_current], LV_OBJ_FLAG_HIDDEN);

    /* Advance */
    s_current = (s_current + 1) % MODE_COUNT;

    /* Show transition label */
    lv_label_set_text(s_transition_label, s_mode_names[s_current]);
    lv_obj_clear_flag(s_transition_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_transition_label);
    s_transition_end_us = now + TRANSITION_SHOW_US;
    s_in_transition = true;

    /* Show new mode container */
    lv_obj_clear_flag(s_mode_containers[s_current], LV_OBJ_FLAG_HIDDEN);

    /* Enter new mode */
    enter_mode(s_current);
    s_session_start_us = now;

    ESP_LOGI(TAG, "Switched to mode: %s", s_mode_names[s_current]);
}

play_mode_t mode_manager_current(void)
{
    return s_current;
}

void mode_manager_update(const imu_state_t *imu)
{
    int64_t now = esp_timer_get_time();

    /* Hide transition label after timeout */
    if (s_in_transition && now >= s_transition_end_us) {
        lv_obj_add_flag(s_transition_label, LV_OBJ_FLAG_HIDDEN);
        s_in_transition = false;
    }

    /* Session timer — restart when expired (could show idle screen, but just restart) */
    if (now - s_session_start_us > SESSION_DURATION_US) {
        /* Reset session */
        s_session_start_us = now;
        exit_mode(s_current);
        enter_mode(s_current);
        ESP_LOGI(TAG, "Session reset for %s", s_mode_names[s_current]);
    }

    /* Update active mode */
    switch (s_current) {
    case MODE_SNOW_GLOBE:  mode_snow_globe_update(imu); break;
    case MODE_DANCE_PARTY: mode_dance_update(imu); break;
    case MODE_MUSIC_MAKER: mode_music_update(imu); break;
    default: break;
    }
}

void mode_manager_on_touch(int16_t x, int16_t y)
{
    /* Reset session timer on touch */
    s_session_start_us = esp_timer_get_time();

    if (s_current == MODE_MUSIC_MAKER) {
        mode_music_on_touch(x, y);
    }
}

void mode_manager_on_release(void)
{
    if (s_current == MODE_MUSIC_MAKER) {
        mode_music_on_release();
    }
}
