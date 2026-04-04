/*
 * ui_monitor.c — LVGL baby monitor UI for 368x448 AMOLED
 *
 * Layout (top to bottom):
 * - Title: "Baby Monitor (VAD)"
 * - Large status text (color-coded)
 * - VAD indicator
 * - Audio level bar
 * - Detection stats
 * - Battery info
 * - WiFi status
 */

#include "ui_monitor.h"
#include "cry_detector.h"
#include "audio_capture.h"
#include "vad_filter.h"
#include "amoled.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_mon";

/* ── LVGL widget handles ──────────────────────────────────── */

static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_lbl_title    = NULL;
static lv_obj_t *s_lbl_status   = NULL;
static lv_obj_t *s_lbl_vad      = NULL;
static lv_obj_t *s_bar_audio    = NULL;
static lv_obj_t *s_lbl_stats    = NULL;
static lv_obj_t *s_lbl_battery  = NULL;
static lv_obj_t *s_lbl_wifi     = NULL;
static lv_obj_t *s_lbl_impl     = NULL;

/* Shared state updated from outside LVGL context */
static char s_ip_str[20] = "";
static bool s_wifi_connected = false;

/* ── UI creation ──────────────────────────────────────────── */

void ui_monitor_init(void)
{
    s_screen = lv_scr_act();
    lv_obj_set_style_bg_color(s_screen, lv_color_make(10, 10, 20), 0);

    /* Title */
    s_lbl_title = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_title, "Baby Monitor (VAD)");
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_make(180, 200, 255), 0);
    lv_obj_align(s_lbl_title, LV_ALIGN_TOP_MID, 0, 10);

    /* VAD implementation label */
    s_lbl_impl = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_impl, "VAD: Energy-based");
    lv_obj_set_style_text_font(s_lbl_impl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_impl, lv_color_make(150, 150, 150), 0);
    lv_obj_align(s_lbl_impl, LV_ALIGN_TOP_MID, 0, 36);

    /* Large status text */
    s_lbl_status = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_status, "Initializing...");
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_make(100, 200, 100), 0);
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_MID, 0, 70);

    /* VAD indicator */
    s_lbl_vad = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_vad, "VAD: --");
    lv_obj_set_style_text_font(s_lbl_vad, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_vad, lv_color_make(120, 120, 120), 0);
    lv_obj_align(s_lbl_vad, LV_ALIGN_TOP_MID, 0, 115);

    /* Audio level bar */
    s_bar_audio = lv_bar_create(s_screen);
    lv_obj_set_size(s_bar_audio, 300, 20);
    lv_bar_set_range(s_bar_audio, 0, 100);
    lv_bar_set_value(s_bar_audio, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_audio, lv_color_make(40, 40, 60), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_audio, lv_color_make(60, 180, 60), LV_PART_INDICATOR);
    lv_obj_align(s_bar_audio, LV_ALIGN_TOP_MID, 0, 155);

    /* Audio level label */
    lv_obj_t *lbl_audio = lv_label_create(s_screen);
    lv_label_set_text(lbl_audio, "Audio Level");
    lv_obj_set_style_text_font(lbl_audio, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_audio, lv_color_make(150, 150, 150), 0);
    lv_obj_align(lbl_audio, LV_ALIGN_TOP_MID, 0, 180);

    /* Detection stats */
    s_lbl_stats = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_stats, "Events: 0\nBand ratio: --\nRMS: --");
    lv_obj_set_style_text_font(s_lbl_stats, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_stats, lv_color_make(200, 200, 200), 0);
    lv_obj_align(s_lbl_stats, LV_ALIGN_TOP_LEFT, 20, 210);

    /* Separator line */
    lv_obj_t *line = lv_obj_create(s_screen);
    lv_obj_set_size(line, 328, 2);
    lv_obj_set_style_bg_color(line, lv_color_make(60, 60, 80), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 310);

    /* Battery info */
    s_lbl_battery = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_battery, "Battery: --");
    lv_obj_set_style_text_font(s_lbl_battery, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_battery, lv_color_make(200, 200, 200), 0);
    lv_obj_align(s_lbl_battery, LV_ALIGN_TOP_LEFT, 20, 325);

    /* WiFi status */
    s_lbl_wifi = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_wifi, "WiFi: Not connected");
    lv_obj_set_style_text_font(s_lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_wifi, lv_color_make(200, 200, 200), 0);
    lv_obj_align(s_lbl_wifi, LV_ALIGN_TOP_LEFT, 20, 350);

    ESP_LOGI(TAG, "UI monitor initialized");
}

void ui_monitor_show_connecting(void)
{
    /* This is called from app_main before LVGL task starts, so direct call is OK */
    if (s_lbl_status) {
        lv_label_set_text(s_lbl_status, "Connecting...");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_make(255, 200, 50), 0);
    }
    if (s_lbl_wifi) {
        lv_label_set_text(s_lbl_wifi, "WiFi: Connecting...");
    }
}

void ui_monitor_show_ip(const char *ip)
{
    if (ip) {
        strncpy(s_ip_str, ip, sizeof(s_ip_str) - 1);
        s_wifi_connected = true;
    }
}

/* ── Timer callback (runs in LVGL context) ────────────────── */

void ui_monitor_timer_cb(struct _lv_timer_t *timer)
{
    (void)timer;

    /* Get detection status */
    cry_detector_status_t status;
    cry_detector_get_status(&status);

    /* Update main status text */
    switch (status.state) {
        case CRY_STATE_IDLE:
            lv_label_set_text(s_lbl_status, "Listening...");
            lv_obj_set_style_text_color(s_lbl_status,
                                        lv_color_make(100, 200, 100), 0);
            break;
        case CRY_STATE_VOICE:
            lv_label_set_text(s_lbl_status, "Voice Detected");
            lv_obj_set_style_text_color(s_lbl_status,
                                        lv_color_make(255, 200, 50), 0);
            break;
        case CRY_STATE_DETECTED:
            lv_label_set_text(s_lbl_status, "CRYING!");
            lv_obj_set_style_text_color(s_lbl_status,
                                        lv_color_make(255, 50, 50), 0);
            break;
    }

    /* Update VAD indicator */
    if (status.vad_active) {
        lv_label_set_text(s_lbl_vad, "VAD: Voice Active");
        lv_obj_set_style_text_color(s_lbl_vad,
                                    lv_color_make(50, 255, 50), 0);
    } else {
        lv_label_set_text(s_lbl_vad, "VAD: Silent");
        lv_obj_set_style_text_color(s_lbl_vad,
                                    lv_color_make(120, 120, 120), 0);
    }

    /* Update audio level bar (RMS 0-32767 → 0-100) */
    int16_t rms = audio_capture_get_rms();
    int bar_val = (rms * 100) / 4000;  /* Scale: 4000 RMS ≈ full bar */
    if (bar_val > 100) bar_val = 100;
    lv_bar_set_value(s_bar_audio, bar_val, LV_ANIM_ON);

    /* Color the bar based on level */
    if (bar_val > 70) {
        lv_obj_set_style_bg_color(s_bar_audio,
                                  lv_color_make(255, 60, 60), LV_PART_INDICATOR);
    } else if (bar_val > 40) {
        lv_obj_set_style_bg_color(s_bar_audio,
                                  lv_color_make(255, 200, 50), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(s_bar_audio,
                                  lv_color_make(60, 180, 60), LV_PART_INDICATOR);
    }

    /* Update stats */
    char stats_buf[160];
    if (status.last_cry_time > 0) {
        int64_t elapsed_us = esp_timer_get_time() - status.last_cry_time;
        int elapsed_s = (int)(elapsed_us / 1000000);
        int mins = elapsed_s / 60;
        int secs = elapsed_s % 60;
        snprintf(stats_buf, sizeof(stats_buf),
                 "Events: %lu\nBand ratio: %.1f%%\nRMS: %.0f\nLast cry: %dm %ds ago",
                 (unsigned long)status.cry_count,
                 status.cry_band_ratio * 100.0f,
                 status.rms_energy,
                 mins, secs);
    } else {
        snprintf(stats_buf, sizeof(stats_buf),
                 "Events: %lu\nBand ratio: %.1f%%\nRMS: %.0f",
                 (unsigned long)status.cry_count,
                 status.cry_band_ratio * 100.0f,
                 status.rms_energy);
    }
    lv_label_set_text(s_lbl_stats, stats_buf);

    /* Update battery (every ~2 seconds, since timer runs at 200ms) */
    static int batt_counter = 0;
    if (++batt_counter >= 10) {
        batt_counter = 0;
        amoled_battery_info_t batt;
        if (amoled_get_battery_info(&batt) == ESP_OK) {
            char batt_buf[64];
            snprintf(batt_buf, sizeof(batt_buf),
                     "Battery: %u mV  %u%%%s",
                     batt.voltage_mv, batt.percentage,
                     batt.charging ? " (charging)" : "");
            lv_label_set_text(s_lbl_battery, batt_buf);
        }
    }

    /* Update WiFi status */
    if (s_wifi_connected) {
        char wifi_buf[48];
        snprintf(wifi_buf, sizeof(wifi_buf), "WiFi: %s", s_ip_str);
        lv_label_set_text(s_lbl_wifi, wifi_buf);
        lv_obj_set_style_text_color(s_lbl_wifi,
                                    lv_color_make(100, 255, 100), 0);
    }

    /* Update VAD implementation label */
    vad_impl_t impl = vad_filter_get_impl();
    if (impl == VAD_IMPL_ENERGY) {
        lv_label_set_text(s_lbl_impl, "VAD: Energy-based (fallback)");
    } else {
        lv_label_set_text(s_lbl_impl, "VAD: ESP-SR VADNet");
    }
}
