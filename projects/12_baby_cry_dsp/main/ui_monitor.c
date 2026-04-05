/*
 * ui_monitor.c — Enhanced LVGL baby monitor UI on 368x448 AMOLED
 *
 * Layout (top to bottom):
 *   - Title "Baby Monitor" (28px)
 *   - Status: "Listening..." / "CRYING!" (36px, color-coded)
 *   - Audio RMS level bar (wide)
 *   - FFT Spectrum visualization (32 bars, cry band highlighted)
 *   - Detection info: cry ratio, periodicity, threshold
 *   - Separator line
 *   - System status grid: Battery | WiFi | NTP | SD
 *   - Cry event stats: count, last time
 */

#include "ui_monitor.h"
#include "cry_detector.h"
#include "audio_capture.h"
#include "ntp_time.h"
#include "sd_logger.h"
#include "amoled.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_mon";

/* ── Screen Layout Constants ──────────────────────────────── */

#define SCREEN_W    368
#define SCREEN_H    448
#define MARGIN      12
#define BAR_W       (SCREEN_W - 2 * MARGIN)

/* Spectrum visualization */
#define SPEC_BARS       CRY_SPECTRUM_BINS  /* 32 */
#define SPEC_BAR_W      9     /* (BAR_W - gaps) / 32 ≈ 10 */
#define SPEC_BAR_GAP    2
#define SPEC_HEIGHT     80
#define SPEC_Y          168

/* ── UI Widgets ───────────────────────────────────────────── */

static lv_obj_t *s_scr           = NULL;
static lv_obj_t *s_lbl_title     = NULL;
static lv_obj_t *s_lbl_status    = NULL;
static lv_obj_t *s_bar_audio     = NULL;
static lv_obj_t *s_lbl_rms       = NULL;
static lv_obj_t *s_lbl_detail    = NULL;
static lv_obj_t *s_lbl_battery   = NULL;
static lv_obj_t *s_lbl_wifi      = NULL;
static lv_obj_t *s_lbl_ntp       = NULL;
static lv_obj_t *s_lbl_sd        = NULL;
static lv_obj_t *s_lbl_stats     = NULL;
static lv_obj_t *s_lbl_btn      = NULL;
static lv_obj_t *s_lbl_logs     = NULL;

/* Button state (set from main, read from timer cb) */
static volatile int s_btn_count = 0;
static volatile int s_btn_brightness = 200;

/* Spectrum bars (array of rectangle objects) */
static lv_obj_t *s_spec_bars[SPEC_BARS];
static lv_obj_t *s_spec_container = NULL;

/* ── Shared State ─────────────────────────────────────────── */

static char s_ip_str[20] = "";
static bool s_connecting = true;
static uint32_t s_last_logged_cry = 0;
static uint32_t s_last_logged_burst = 0;

/* ── Helpers ──────────────────────────────────────────────── */

static lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font,
                               lv_color_t color, lv_align_t align,
                               lv_coord_t x_ofs, lv_coord_t y_ofs,
                               const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_align(lbl, align, x_ofs, y_ofs);
    return lbl;
}

/* ── UI Creation ──────────────────────────────────────────── */

void ui_monitor_init(void)
{
    s_scr = lv_scr_act();
    lv_obj_set_style_bg_color(s_scr, lv_color_make(0, 0, 0), 0);

    int y = 10;

    /* Title */
    s_lbl_title = create_label(s_scr, &lv_font_montserrat_20,
                                lv_color_make(100, 160, 220),
                                LV_ALIGN_TOP_MID, 0, y, "Baby Monitor (DSP)");
    y += 30;

    /* Status (large) */
    s_lbl_status = create_label(s_scr, &lv_font_montserrat_36,
                                 lv_color_make(100, 100, 100),
                                 LV_ALIGN_TOP_MID, 0, y, "Initializing...");
    y += 46;

    /* RMS label (small, left-aligned) */
    s_lbl_rms = create_label(s_scr, &lv_font_montserrat_14,
                              lv_color_make(100, 100, 100),
                              LV_ALIGN_TOP_LEFT, MARGIN, y, "Level: --");

    /* Audio level bar */
    y += 18;
    s_bar_audio = lv_bar_create(s_scr);
    lv_obj_set_size(s_bar_audio, BAR_W, 14);
    lv_obj_align(s_bar_audio, LV_ALIGN_TOP_LEFT, MARGIN, y);
    lv_bar_set_range(s_bar_audio, 0, 100);
    lv_bar_set_value(s_bar_audio, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar_audio, lv_color_make(30, 30, 30), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_audio, lv_color_make(0, 180, 60), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar_audio, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar_audio, 4, LV_PART_INDICATOR);
    y += 22;

    /* "Spectrum" label */
    create_label(s_scr, &lv_font_montserrat_14,
                 lv_color_make(80, 80, 80),
                 LV_ALIGN_TOP_LEFT, MARGIN, y, "FFT Spectrum (0-8kHz)");
    y += 18;

    /* Spectrum bar container */
    s_spec_container = lv_obj_create(s_scr);
    lv_obj_set_size(s_spec_container, BAR_W, SPEC_HEIGHT);
    lv_obj_align(s_spec_container, LV_ALIGN_TOP_LEFT, MARGIN, y);
    lv_obj_set_style_bg_color(s_spec_container, lv_color_make(10, 10, 15), 0);
    lv_obj_set_style_border_width(s_spec_container, 1, 0);
    lv_obj_set_style_border_color(s_spec_container, lv_color_make(40, 40, 50), 0);
    lv_obj_set_style_radius(s_spec_container, 4, 0);
    lv_obj_set_style_pad_all(s_spec_container, 2, 0);
    lv_obj_clear_flag(s_spec_container, LV_OBJ_FLAG_SCROLLABLE);

    /* Create individual spectrum bars */
    for (int i = 0; i < SPEC_BARS; i++) {
        s_spec_bars[i] = lv_obj_create(s_spec_container);
        lv_obj_set_size(s_spec_bars[i], SPEC_BAR_W, 2);
        lv_obj_set_pos(s_spec_bars[i], i * (SPEC_BAR_W + SPEC_BAR_GAP), SPEC_HEIGHT - 8);
        lv_obj_set_style_bg_color(s_spec_bars[i], lv_color_make(0, 120, 200), 0);
        lv_obj_set_style_bg_opa(s_spec_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_spec_bars[i], 0, 0);
        lv_obj_set_style_radius(s_spec_bars[i], 1, 0);
        lv_obj_clear_flag(s_spec_bars[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    y += SPEC_HEIGHT + 4;

    /* Detection detail */
    s_lbl_detail = create_label(s_scr, &lv_font_montserrat_14,
                                 lv_color_make(120, 120, 120),
                                 LV_ALIGN_TOP_LEFT, MARGIN, y, "");
    y += 20;

    /* Separator line */
    lv_obj_t *line = lv_obj_create(s_scr);
    lv_obj_set_size(line, BAR_W, 1);
    lv_obj_align(line, LV_ALIGN_TOP_LEFT, MARGIN, y);
    lv_obj_set_style_bg_color(line, lv_color_make(50, 50, 60), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    y += 8;

    /* Status grid — two columns */
    lv_color_t dim = lv_color_make(160, 160, 170);

    s_lbl_battery = create_label(s_scr, &lv_font_montserrat_14, dim,
                                  LV_ALIGN_TOP_LEFT, MARGIN, y, "Batt: --");
    s_lbl_wifi = create_label(s_scr, &lv_font_montserrat_14, dim,
                               LV_ALIGN_TOP_LEFT, SCREEN_W / 2, y, "WiFi: --");
    y += 20;

    s_lbl_ntp = create_label(s_scr, &lv_font_montserrat_14, dim,
                              LV_ALIGN_TOP_LEFT, MARGIN, y, "NTP: --");
    s_lbl_sd = create_label(s_scr, &lv_font_montserrat_14, dim,
                             LV_ALIGN_TOP_LEFT, SCREEN_W / 2, y, "SD: --");
    y += 24;

    /* Cry event stats */
    s_lbl_stats = create_label(s_scr, &lv_font_montserrat_14,
                                lv_color_make(200, 200, 200),
                                LV_ALIGN_TOP_LEFT, MARGIN, y, "Events: 0");
    y += 24;

    /* Detection metrics (Torres features + score) */
    s_lbl_btn = create_label(s_scr, &lv_font_montserrat_14,
                              lv_color_make(180, 180, 0),
                              LV_ALIGN_TOP_LEFT, MARGIN, y, "H:0% C:0.0 Fv:0.0 Cf:0");
    y += 20;

    /* Log counters */
    s_lbl_logs = create_label(s_scr, &lv_font_montserrat_14,
                               lv_color_make(140, 180, 140),
                               LV_ALIGN_TOP_LEFT, MARGIN, y, "Logs: 0 metrics, 0 cries");
    y += 20;

    /* Build version at bottom */
    char build_str[48];
    snprintf(build_str, sizeof(build_str), "Build: %s %s", __DATE__, __TIME__);
    create_label(s_scr, &lv_font_montserrat_14,
                 lv_color_make(60, 60, 70),
                 LV_ALIGN_TOP_LEFT, MARGIN, SCREEN_H - 20, build_str);

    ESP_LOGI(TAG, "UI initialized (enhanced with spectrum + status grid)");
}

/* ── Status Updates ───────────────────────────────────────── */

void ui_monitor_show_connecting(void)
{
    s_connecting = true;
}

void ui_monitor_show_ip(const char *ip)
{
    if (ip) {
        strncpy(s_ip_str, ip, sizeof(s_ip_str) - 1);
        s_ip_str[sizeof(s_ip_str) - 1] = '\0';
    }
    s_connecting = false;
}

/* ── LVGL Timer Callback ─────────────────────────────────── */

void ui_monitor_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    char buf[96];
    cry_detector_status_t status;
    cry_detector_get_status(&status);

    /* ── Main status ────────────────────────────────── */
    if (s_connecting) {
        lv_label_set_text(s_lbl_status, "Connecting...");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_make(255, 200, 0), 0);
    } else if (status.state == CRY_STATE_DETECTED) {
        lv_label_set_text(s_lbl_status, "CRYING!");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_make(255, 40, 40), 0);
        lv_obj_set_style_bg_color(s_bar_audio, lv_color_make(255, 40, 40), LV_PART_INDICATOR);
    } else {
        lv_label_set_text(s_lbl_status, "Listening...");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_make(0, 220, 80), 0);
        lv_obj_set_style_bg_color(s_bar_audio, lv_color_make(0, 180, 60), LV_PART_INDICATOR);
    }

    /* ── Audio level bar (auto-scaled to actual mic range) ── */
    int16_t rms = audio_capture_get_rms();
    /* Auto-scale: map 0-500 RMS to 0-100 bar (MEMS mic typical range) */
    int level = rms / 5;
    if (level > 100) level = 100;
    lv_bar_set_value(s_bar_audio, level, LV_ANIM_OFF);

    snprintf(buf, sizeof(buf), "Lv:%d RMS:%.0f NF:%.0f Raw:%d",
             level, status.rms_energy, status.noise_floor, rms);
    lv_label_set_text(s_lbl_rms, buf);

    /* ── FFT Spectrum bars ──────────────────────────── */
    int max_bar_h = SPEC_HEIGHT - 10;
    for (int i = 0; i < SPEC_BARS; i++) {
        int h = (int)status.spectrum[i] * max_bar_h / 255;
        if (h < 2) h = 2;
        lv_obj_set_height(s_spec_bars[i], h);
        lv_obj_set_y(s_spec_bars[i], max_bar_h - h);

        /* Color: cry band bins = orange/red, others = blue/cyan */
        if (i >= status.cry_bin_low && i <= status.cry_bin_high) {
            if (status.state == CRY_STATE_DETECTED) {
                lv_obj_set_style_bg_color(s_spec_bars[i], lv_color_make(255, 50, 30), 0);
            } else {
                lv_obj_set_style_bg_color(s_spec_bars[i], lv_color_make(255, 160, 0), 0);
            }
        } else {
            lv_obj_set_style_bg_color(s_spec_bars[i], lv_color_make(0, 100, 200), 0);
        }
    }

    /* ── Detection detail ───────────────────────────── */
    snprintf(buf, sizeof(buf), "S:%d Cry:%.0f%% Lo:%.0f%% F0:%dHz V:%.0f%%%s",
             status.score, status.cry_band_ratio * 100.0f,
             status.low_ratio * 100.0f, status.f0_hz,
             status.voiced_ratio * 100.0f,
             status.gated ? " GATED" : "");
    lv_label_set_text(s_lbl_detail, buf);

    /* ── Battery ──────────────────────────────────���─── */
    amoled_battery_info_t batt;
    if (amoled_get_battery_info(&batt) == ESP_OK) {
        snprintf(buf, sizeof(buf), "Batt: %u%% %umV%s",
                 batt.percentage, batt.voltage_mv, batt.charging ? " CHG" : "");
        lv_label_set_text(s_lbl_battery, buf);
    }

    /* ── WiFi ───────────────────────────────────────── */
    if (s_connecting) {
        lv_label_set_text(s_lbl_wifi, "WiFi: ...");
        lv_obj_set_style_text_color(s_lbl_wifi, lv_color_make(255, 200, 0), 0);
    } else if (!ntp_time_is_wifi_on()) {
        lv_label_set_text(s_lbl_wifi, "WiFi: OFF");
        lv_obj_set_style_text_color(s_lbl_wifi, lv_color_make(100, 100, 100), 0);
    } else if (s_ip_str[0]) {
        snprintf(buf, sizeof(buf), "WiFi: %s", s_ip_str);
        lv_label_set_text(s_lbl_wifi, buf);
        lv_obj_set_style_text_color(s_lbl_wifi, lv_color_make(0, 200, 80), 0);
    }

    /* ── NTP ────────────────────────────────────────── */
    if (ntp_time_is_synced()) {
        char ts[16];
        ntp_time_get_str(ts, sizeof(ts));
        snprintf(buf, sizeof(buf), "NTP: %s", ts);
        lv_label_set_text(s_lbl_ntp, buf);
        lv_obj_set_style_text_color(s_lbl_ntp, lv_color_make(0, 200, 80), 0);
    } else {
        lv_label_set_text(s_lbl_ntp, "NTP: sync...");
        lv_obj_set_style_text_color(s_lbl_ntp, lv_color_make(160, 160, 170), 0);
    }

    /* ── Storage status ──────────────────────────────── */
    sd_state_t st = sd_logger_get_state();
    if (st == SD_STATE_MOUNTED) {
        lv_label_set_text(s_lbl_sd, "Log: SPIFFS");
        lv_obj_set_style_text_color(s_lbl_sd, lv_color_make(0, 200, 80), 0);
    } else {
        lv_label_set_text(s_lbl_sd, "Log: --");
        lv_obj_set_style_text_color(s_lbl_sd, lv_color_make(255, 80, 0), 0);
    }

    /* ── Logging (unified: periodic + cry events) ──── */
    sd_logger_check();
    {
        char ts[16];
        ntp_time_get_str(ts, sizeof(ts));

        /* WiFi RSSI */
        wifi_ap_record_t ap_info;
        int rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi = ap_info.rssi;
        }

        /* Periodic metrics (rate-limited inside sd_logger_log) */
        sd_logger_log("periodic", ts, &status,
                      batt.voltage_mv, batt.percentage, batt.charging, batt.vbus_present,
                      (uint32_t)esp_get_free_heap_size(),
                      (uint32_t)esp_get_minimum_free_heap_size(),
                      rssi, (uint8_t)s_btn_brightness);

        /* Cry event snapshot (immediate, not rate-limited) */
        if (status.cry_count > s_last_logged_cry) {
            s_last_logged_cry = status.cry_count;
            sd_logger_log("cry", ts, &status,
                          batt.voltage_mv, batt.percentage, batt.charging, batt.vbus_present,
                          (uint32_t)esp_get_free_heap_size(),
                          (uint32_t)esp_get_minimum_free_heap_size(),
                          rssi, (uint8_t)s_btn_brightness);
        }

        /* Burst event snapshot (short cry that didn't sustain) */
        if (status.burst_count > s_last_logged_burst) {
            s_last_logged_burst = status.burst_count;
            sd_logger_log("burst", ts, &status,
                          batt.voltage_mv, batt.percentage, batt.charging, batt.vbus_present,
                          (uint32_t)esp_get_free_heap_size(),
                          (uint32_t)esp_get_minimum_free_heap_size(),
                          rssi, (uint8_t)s_btn_brightness);
        }
    }

    /* ── Cry event stats ────────────────────────────── */
    if (status.cry_count > 0) {

        int64_t elapsed_us = esp_timer_get_time() - status.last_cry_time;
        int elapsed_s = (int)(elapsed_us / 1000000);
        int min = elapsed_s / 60;
        int sec = elapsed_s % 60;

        char ts_str[16] = "";
        if (ntp_time_is_synced()) {
            /* Show actual time of last cry */
            time_t cry_time = ntp_time_get() - elapsed_s;
            struct tm ti;
            localtime_r(&cry_time, &ti);
            strftime(ts_str, sizeof(ts_str), " (%H:%M)", &ti);
        }

        snprintf(buf, sizeof(buf), "Cry:%lu Burst:%lu  Last: %dm%02ds ago%s",
                 (unsigned long)status.cry_count,
                 (unsigned long)status.burst_count, min, sec, ts_str);
    } else if (status.burst_count > 0) {
        snprintf(buf, sizeof(buf), "Cry:0 Burst:%lu  Monitoring...",
                 (unsigned long)status.burst_count);
    } else {
        snprintf(buf, sizeof(buf), "Cry:0 Burst:0  Monitoring...");
    }
    lv_label_set_text(s_lbl_stats, buf);

    /* ── Log counters ───────────────────────────────── */
    snprintf(buf, sizeof(buf), "Log: %lu met %lu cry  SD: %lu exp",
             (unsigned long)sd_logger_get_metrics_count(),
             (unsigned long)sd_logger_get_cry_count(),
             (unsigned long)sd_logger_get_sd_export_count());
    lv_label_set_text(s_lbl_logs, buf);

    /* ── Detection metrics (Torres features) ──────── */
    snprintf(buf, sizeof(buf), "H:%d%% C:%.1f Fv:%.1f Cf:%d Prd:%d%s",
             status.harmonic_pct, status.crest,
             status.f0_variance, status.max_consec_f0,
             status.periodicity,
             status.cry_dominant ? " DOM" : "");
    lv_label_set_text(s_lbl_btn, buf);
}

void ui_monitor_set_btn_count(int count, int brightness)
{
    s_btn_count = count;
    s_btn_brightness = brightness;
}
