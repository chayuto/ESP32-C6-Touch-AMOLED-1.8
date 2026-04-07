/*
 * ui_dashboard.c — LVGL telemetry dashboard for BitChat relay
 *
 * Layout (368×448 portrait):
 *   Title "BitChat Relay" (28px)
 *   Stats grid: Peers | Conns | Pkts/s
 *   Packet counters: RX / TX / Drop / Expire
 *   TTL histogram (8 bars)
 *   Peer list (scrollable, shows nickname + RSSI + pkt count)
 *   Bottom bar: Battery | Heap | SD | Bloom
 */

#include "ui_dashboard.h"
#include "telemetry.h"
#include "ble_relay.h"
#include "sd_logger.h"
#include "amoled.h"

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_dash";

#define SCREEN_W    368
#define SCREEN_H    448
#define MARGIN      8

/* ── Widgets ─────────────────────────────────────────────── */

static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_stats;      /* Peers / Conns / Pkts/s */
static lv_obj_t *s_lbl_counters;   /* RX / TX / Drop / Expire */
static lv_obj_t *s_lbl_types;      /* Message type breakdown */
static lv_obj_t *s_lbl_peers;      /* Peer list */
static lv_obj_t *s_lbl_system;     /* Battery / Heap / SD / Bloom */
static lv_obj_t *s_lbl_sd_status;  /* Temporary SD export message */

/* TTL histogram bars */
#define TTL_BARS 8
#define TTL_BAR_W  ((SCREEN_W - 2 * MARGIN - (TTL_BARS - 1) * 3) / TTL_BARS)
#define TTL_BAR_H  40
static lv_obj_t *s_ttl_bars[TTL_BARS];
static lv_obj_t *s_lbl_ttl_title;

static int64_t s_sd_status_expire = 0;

/* ── Colors ──────────────────────────────────────────────── */

static lv_color_t col_bg(void)       { return lv_color_make(10, 10, 15); }
static lv_color_t col_title(void)    { return lv_color_make(0, 200, 255); }
static lv_color_t col_text(void)     { return lv_color_make(200, 200, 210); }
static lv_color_t col_accent(void)   { return lv_color_make(0, 255, 120); }
static lv_color_t col_warn(void)     { return lv_color_make(255, 180, 0); }
static lv_color_t col_bar_bg(void)   { return lv_color_make(30, 30, 40); }
static lv_color_t col_bar_fill(void) { return lv_color_make(0, 150, 255); }

/* ── Init ────────────────────────────────────────────────── */

void ui_dashboard_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, col_bg(), 0);

    int y = MARGIN;

    /* Title */
    s_lbl_title = lv_label_create(scr);
    lv_label_set_text(s_lbl_title, "BitChat Relay");
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_lbl_title, col_title(), 0);
    lv_obj_set_pos(s_lbl_title, MARGIN, y);
    y += 34;

    /* Stats line: Peers / Conns / Pkts/s */
    s_lbl_stats = lv_label_create(scr);
    lv_label_set_text(s_lbl_stats, "Peers:0  Conns:0  0.0 pkt/s");
    lv_obj_set_style_text_font(s_lbl_stats, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_stats, col_accent(), 0);
    lv_obj_set_pos(s_lbl_stats, MARGIN, y);
    y += 26;

    /* Packet counters */
    s_lbl_counters = lv_label_create(scr);
    lv_label_set_text(s_lbl_counters, "RX:0 TX:0 Drop:0 Exp:0");
    lv_obj_set_style_text_font(s_lbl_counters, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_counters, col_text(), 0);
    lv_obj_set_pos(s_lbl_counters, MARGIN, y);
    y += 20;

    /* Message type breakdown */
    s_lbl_types = lv_label_create(scr);
    lv_label_set_text(s_lbl_types, "Msg:0 Ack:0 Frag:0 Bcast:0 DM:0");
    lv_obj_set_style_text_font(s_lbl_types, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_types, col_text(), 0);
    lv_obj_set_pos(s_lbl_types, MARGIN, y);
    y += 22;

    /* TTL histogram title */
    s_lbl_ttl_title = lv_label_create(scr);
    lv_label_set_text(s_lbl_ttl_title, "TTL Distribution");
    lv_obj_set_style_text_font(s_lbl_ttl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_ttl_title, col_text(), 0);
    lv_obj_set_pos(s_lbl_ttl_title, MARGIN, y);
    y += 18;

    /* TTL histogram bars */
    for (int i = 0; i < TTL_BARS; i++) {
        int x = MARGIN + i * (TTL_BAR_W + 3);
        s_ttl_bars[i] = lv_bar_create(scr);
        lv_obj_set_size(s_ttl_bars[i], TTL_BAR_W, TTL_BAR_H);
        lv_obj_set_pos(s_ttl_bars[i], x, y);
        lv_bar_set_range(s_ttl_bars[i], 0, 100);
        lv_bar_set_value(s_ttl_bars[i], 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(s_ttl_bars[i], col_bar_bg(), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_ttl_bars[i], col_bar_fill(), LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_ttl_bars[i], 2, 0);
    }
    y += TTL_BAR_H + 6;

    /* Peer list (scrollable area) */
    s_lbl_peers = lv_label_create(scr);
    lv_label_set_text(s_lbl_peers, "No peers yet");
    lv_obj_set_style_text_font(s_lbl_peers, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_peers, col_text(), 0);
    lv_obj_set_pos(s_lbl_peers, MARGIN, y);
    lv_obj_set_width(s_lbl_peers, SCREEN_W - 2 * MARGIN);
    lv_label_set_long_mode(s_lbl_peers, LV_LABEL_LONG_WRAP);
    y += 120;

    /* System status bar */
    s_lbl_system = lv_label_create(scr);
    lv_label_set_text(s_lbl_system, "Heap:? SD:? Bloom:0%");
    lv_obj_set_style_text_font(s_lbl_system, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_system, col_text(), 0);
    lv_obj_set_pos(s_lbl_system, MARGIN, SCREEN_H - 40);
    lv_obj_set_width(s_lbl_system, SCREEN_W - 2 * MARGIN);

    /* SD status overlay (hidden by default) */
    s_lbl_sd_status = lv_label_create(scr);
    lv_label_set_text(s_lbl_sd_status, "");
    lv_obj_set_style_text_font(s_lbl_sd_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_sd_status, col_warn(), 0);
    lv_obj_set_pos(s_lbl_sd_status, MARGIN, SCREEN_H - 60);
    lv_obj_add_flag(s_lbl_sd_status, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Dashboard UI created");
}

/* ── Timer callback (500ms) ──────────────────────────────── */

void ui_dashboard_timer_cb(lv_timer_t *timer)
{
    telemetry_t t;
    telemetry_update();
    telemetry_get_snapshot(&t);

    char buf[256];

    /* Stats line */
    snprintf(buf, sizeof(buf), "Peers:%u  Conns:%u  %.1f pkt/s",
             t.peer_count, ble_relay_connection_count(), t.packets_per_sec);
    lv_label_set_text(s_lbl_stats, buf);

    /* Counters */
    snprintf(buf, sizeof(buf), "RX:%lu TX:%lu Drop:%lu Exp:%lu Buf:%lu",
             (unsigned long)t.packets_rx, (unsigned long)t.packets_tx,
             (unsigned long)t.packets_dropped, (unsigned long)t.packets_expired,
             (unsigned long)t.packets_buffered);
    lv_label_set_text(s_lbl_counters, buf);

    /* Message type breakdown */
    uint32_t frag_total = t.msg_type_counts[3] + t.msg_type_counts[4] + t.msg_type_counts[5];
    snprintf(buf, sizeof(buf), "Msg:%lu Ack:%lu Frag:%lu Bcast:%lu DM:%lu Sig:%lu",
             (unsigned long)t.msg_type_counts[0],
             (unsigned long)t.msg_type_counts[1],
             (unsigned long)frag_total,
             (unsigned long)t.broadcasts,
             (unsigned long)t.directed,
             (unsigned long)t.signed_packets);
    lv_label_set_text(s_lbl_types, buf);

    /* TTL histogram — normalize to percentages */
    uint32_t ttl_max = 1;
    for (int i = 0; i < TTL_BARS; i++) {
        if (t.ttl_histogram[i] > ttl_max) ttl_max = t.ttl_histogram[i];
    }
    for (int i = 0; i < TTL_BARS; i++) {
        int pct = (int)(t.ttl_histogram[i] * 100 / ttl_max);
        lv_bar_set_value(s_ttl_bars[i], pct, LV_ANIM_OFF);
    }

    /* Peer list */
    buf[0] = '\0';
    int pos = 0;
    int shown = 0;
    for (int i = 0; i < TELEMETRY_MAX_PEERS && shown < 8; i++) {
        if (!t.peers[i].active) continue;
        int n = snprintf(buf + pos, sizeof(buf) - pos,
                         "%s%s %ddBm %lupkt",
                         shown > 0 ? "\n" : "",
                         t.peers[i].nickname[0] ? t.peers[i].nickname :
                             "(unknown)",
                         t.peers[i].rssi,
                         (unsigned long)t.peers[i].packets_from);
        if (n > 0) pos += n;
        shown++;
    }
    if (shown == 0) {
        lv_label_set_text(s_lbl_peers, "Scanning for peers...");
    } else {
        lv_label_set_text(s_lbl_peers, buf);
    }

    /* System status */
    amoled_battery_info_t batt;
    amoled_get_battery_info(&batt);

    snprintf(buf, sizeof(buf),
             "%umV %u%% %s  Heap:%luK  SD:%lu  Bloom:%.0f%%  Log:%lu",
             batt.voltage_mv, batt.percentage,
             batt.charging ? "CHG" : "",
             (unsigned long)(t.free_heap / 1024),
             (unsigned long)t.sd_flush_count,
             t.bloom_fill_pct,
             (unsigned long)t.spiffs_rows);
    lv_label_set_text(s_lbl_system, buf);

    /* Clear SD status message after timeout */
    if (!lv_obj_has_flag(s_lbl_sd_status, LV_OBJ_FLAG_HIDDEN)) {
        int64_t now = esp_timer_get_time();
        if (now > s_sd_status_expire) {
            lv_obj_add_flag(s_lbl_sd_status, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_dashboard_show_sd_status(const char *msg)
{
    lv_label_set_text(s_lbl_sd_status, msg);
    lv_obj_clear_flag(s_lbl_sd_status, LV_OBJ_FLAG_HIDDEN);
    s_sd_status_expire = esp_timer_get_time() + 3000000; /* 3 seconds */
}
