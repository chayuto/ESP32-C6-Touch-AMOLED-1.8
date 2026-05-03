/*
 * ui_noise.c — White-noise view (full screen).
 *
 * Layout (368×448):
 *   [<]  White Noise  [batt]      back / title / battery
 *   [Pink ] [Brown]                voice picker (2×2)
 *   [Womb ] [White]
 *           ( Play )                big play/pause button
 *      −  Vol: 60  +                volume up/down
 *         Timer: ∞                  timer cycle (15/30/60/120/∞)
 *
 * Lifecycle: built once at boot (hidden), shown when the user taps the
 * "White Noise" row in the song list. Calls noise_player_* APIs.
 */

#include "ui_noise.h"
#include "noise_player.h"
#include "amoled.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include <stdio.h>
#include <string.h>

extern void ui_show_song_list_from_noise(void);

static const char *TAG = "ui_noise";

#define SCR_W   368
#define SCR_H   448

#ifndef CONFIG_NOISE_VOL_CAP
#define CONFIG_NOISE_VOL_CAP 80
#endif

/* Same palette as ui.c */
#define COL_BG           lv_color_make(15, 12, 35)
#define COL_HEADER_BG    lv_color_make(30, 20, 60)
#define COL_ITEM_BG      lv_color_make(25, 25, 55)
#define COL_ITEM_PRESSED lv_color_make(60, 40, 100)
#define COL_VOICE_ACTIVE lv_color_make(120, 80, 220)
#define COL_PLAY_IDLE     lv_color_make(60, 130, 90)
#define COL_PLAY_ACTIVE   lv_color_make(200, 100, 60)
#define COL_PLAY_STOPPING lv_color_make(120, 100, 100)   /* dim while fading out */
#define COL_TEXT         lv_color_white()
#define COL_TEXT_DIM     lv_color_make(160, 160, 180)
#define COL_NOTE         lv_color_make(255, 200, 80)

/* Timer presets, minutes. 0 sentinel = ∞. */
static const uint16_t TIMER_PRESETS[] = {0, 15, 30, 60, 120};
#define TIMER_PRESET_COUNT (sizeof(TIMER_PRESETS) / sizeof(TIMER_PRESETS[0]))

/* ── State ─────────────────────────────────────────────────────────── */

static lv_obj_t *s_view         = NULL;
static lv_obj_t *s_back_btn     = NULL;
static lv_obj_t *s_batt_lbl     = NULL;
static lv_obj_t *s_voice_btns[NOISE_VOICE_COUNT];
static lv_obj_t *s_voice_lbls[NOISE_VOICE_COUNT];
static lv_obj_t *s_play_btn     = NULL;
static lv_obj_t *s_play_icon    = NULL;
static lv_obj_t *s_vol_dn       = NULL;
static lv_obj_t *s_vol_up       = NULL;
static lv_obj_t *s_vol_lbl      = NULL;
static lv_obj_t *s_timer_btn    = NULL;
static lv_obj_t *s_timer_lbl    = NULL;

static int      s_timer_idx     = 0;        /* index into TIMER_PRESETS, default ∞ */
static uint8_t  s_vol_pct       = 60;       /* 0..100 (clamped to cap) */

/* ── Helpers ───────────────────────────────────────────────────────── */

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                             const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    return lbl;
}

static const char *voice_short_name(noise_voice_t v)
{
    /* 5-char-max for the picker buttons. */
    switch (v) {
    case NOISE_VOICE_PINK:  return "Pink";
    case NOISE_VOICE_BROWN: return "Brown";
    case NOISE_VOICE_WOMB:  return "Womb";
    case NOISE_VOICE_WHITE: return "White";
    default:                return "?";
    }
}

static void apply_voice_highlight(void)
{
    noise_voice_t cur = noise_player_get_voice();
    for (int i = 0; i < NOISE_VOICE_COUNT; i++) {
        if (!s_voice_btns[i]) continue;
        if ((noise_voice_t)i == cur) {
            lv_obj_set_style_bg_color(s_voice_btns[i], COL_VOICE_ACTIVE, 0);
        } else {
            lv_obj_set_style_bg_color(s_voice_btns[i], COL_ITEM_BG, 0);
        }
    }
}

/* ── Event callbacks ───────────────────────────────────────────────── */

static void back_cb(lv_event_t *e)
{
    (void)e;
    /* Don't stop noise — let it keep running so users can browse songs while
     * the room stays soothed. The plan §4 says exclusive routing kicks in
     * only when a *song* starts; the song-list tap callback enforces that. */
    ui_show_song_list_from_noise();
}

static void voice_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    if (v < 0 || v >= NOISE_VOICE_COUNT) return;
    noise_player_set_voice((noise_voice_t)v);
    ESP_LOGI(TAG, "voice → %s", noise_player_voice_name((noise_voice_t)v));
    apply_voice_highlight();
}

static void play_cb(lv_event_t *e)
{
    (void)e;
    if (noise_player_is_playing()) {
        noise_player_stop();
        ESP_LOGI(TAG, "stop pressed");
    } else {
        uint16_t timer_min = TIMER_PRESETS[s_timer_idx];
        if (timer_min == 0) timer_min = NOISE_TIMER_INF;
        noise_player_set_volume(s_vol_pct);
        noise_player_play(noise_player_get_voice(), timer_min);
        ESP_LOGI(TAG, "play pressed (%s, %u min, vol %u%%)",
                 noise_player_voice_name(noise_player_get_voice()),
                 (timer_min == NOISE_TIMER_INF) ? 0u : timer_min,
                 s_vol_pct);
    }
}

static void vol_dn_cb(lv_event_t *e)
{
    (void)e;
    int v = (int)s_vol_pct - 10;
    if (v < 0) v = 0;
    s_vol_pct = (uint8_t)v;
    if (s_vol_pct > CONFIG_NOISE_VOL_CAP) s_vol_pct = CONFIG_NOISE_VOL_CAP;
    noise_player_set_volume(s_vol_pct);
}

static void vol_up_cb(lv_event_t *e)
{
    (void)e;
    int v = (int)s_vol_pct + 10;
    if (v > CONFIG_NOISE_VOL_CAP) v = CONFIG_NOISE_VOL_CAP;
    s_vol_pct = (uint8_t)v;
    noise_player_set_volume(s_vol_pct);
}

static void timer_cb(lv_event_t *e)
{
    (void)e;
    s_timer_idx = (s_timer_idx + 1) % TIMER_PRESET_COUNT;
    /* If currently playing, retarget the live timer. */
    if (noise_player_is_playing()) {
        uint16_t m = TIMER_PRESETS[s_timer_idx];
        noise_player_set_timer(m == 0 ? NOISE_TIMER_INF : m);
    }
}

/* ── Build ─────────────────────────────────────────────────────────── */

void ui_noise_build(lv_obj_t *parent)
{
    s_view = lv_obj_create(parent);
    lv_obj_remove_style_all(s_view);
    lv_obj_set_size(s_view, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_view, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_view, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_view, LV_OBJ_FLAG_SCROLLABLE);

    /* Back arrow (top-left) */
    s_back_btn = lv_obj_create(s_view);
    lv_obj_remove_style_all(s_back_btn);
    lv_obj_set_size(s_back_btn, 50, 40);
    lv_obj_set_pos(s_back_btn, 8, 8);
    lv_obj_add_flag(s_back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *back_lbl = make_label(s_back_btn, LV_SYMBOL_LEFT,
                                    &lv_font_montserrat_18, COL_TEXT);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(s_back_btn, back_cb, LV_EVENT_CLICKED, NULL);

    /* Title */
    lv_obj_t *title = make_label(s_view, "White Noise",
                                  &lv_font_montserrat_22, COL_NOTE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    /* Battery readout (top-right) */
    s_batt_lbl = make_label(s_view, LV_SYMBOL_BATTERY_FULL " 100%",
                             &lv_font_montserrat_12, COL_TEXT_DIM);
    lv_obj_align(s_batt_lbl, LV_ALIGN_TOP_RIGHT, -12, 14);

    /* Voice picker — 2x2 grid */
    const int VBTN_W = 150;
    const int VBTN_H = 50;
    const int VBTN_GAP_X = 14;
    const int VBTN_GAP_Y = 10;
    const int grid_total_w = 2 * VBTN_W + VBTN_GAP_X;
    const int x0 = (SCR_W - grid_total_w) / 2;
    const int y0 = 70;

    /* Order in the grid: Pink, Brown / Womb, White (first row Pink+Brown
     * are the "warm" pair; second row is the experiential pair). */
    static const noise_voice_t grid_order[4] = {
        NOISE_VOICE_PINK, NOISE_VOICE_BROWN,
        NOISE_VOICE_WOMB, NOISE_VOICE_WHITE,
    };

    for (int i = 0; i < 4; i++) {
        int row = i / 2;
        int col = i % 2;
        noise_voice_t v = grid_order[i];

        lv_obj_t *btn = lv_obj_create(s_view);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, VBTN_W, VBTN_H);
        lv_obj_set_pos(btn, x0 + col * (VBTN_W + VBTN_GAP_X),
                            y0 + row * (VBTN_H + VBTN_GAP_Y));
        lv_obj_set_style_bg_color(btn, COL_ITEM_BG, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, COL_ITEM_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = make_label(btn, voice_short_name(v),
                                    &lv_font_montserrat_18, COL_TEXT);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, voice_cb, LV_EVENT_CLICKED, (void *)(intptr_t)v);

        s_voice_btns[(int)v] = btn;
        s_voice_lbls[(int)v] = lbl;
    }
    apply_voice_highlight();

    /* Big play/pause button (centre) */
    s_play_btn = lv_obj_create(s_view);
    lv_obj_remove_style_all(s_play_btn);
    lv_obj_set_size(s_play_btn, 100, 100);
    lv_obj_align(s_play_btn, LV_ALIGN_TOP_MID, 0, 210);
    lv_obj_set_style_bg_color(s_play_btn, COL_PLAY_IDLE, 0);
    lv_obj_set_style_bg_opa(s_play_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_play_btn, 50, 0);
    lv_obj_add_flag(s_play_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_play_btn, LV_OBJ_FLAG_SCROLLABLE);
    s_play_icon = make_label(s_play_btn, LV_SYMBOL_PLAY,
                              &lv_font_montserrat_40, COL_TEXT);
    lv_obj_center(s_play_icon);
    lv_obj_add_event_cb(s_play_btn, play_cb, LV_EVENT_CLICKED, NULL);

    /* Volume row */
    s_vol_dn = lv_obj_create(s_view);
    lv_obj_remove_style_all(s_vol_dn);
    lv_obj_set_size(s_vol_dn, 50, 40);
    lv_obj_align(s_vol_dn, LV_ALIGN_TOP_MID, -90, 330);
    lv_obj_set_style_bg_color(s_vol_dn, COL_ITEM_BG, 0);
    lv_obj_set_style_bg_opa(s_vol_dn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_vol_dn, 8, 0);
    lv_obj_add_flag(s_vol_dn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_vol_dn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *vdn_l = make_label(s_vol_dn, LV_SYMBOL_VOLUME_MID,
                                  &lv_font_montserrat_16, COL_TEXT);
    lv_obj_center(vdn_l);
    lv_obj_add_event_cb(s_vol_dn, vol_dn_cb, LV_EVENT_CLICKED, NULL);

    s_vol_lbl = make_label(s_view, "Vol: 60",
                            &lv_font_montserrat_16, COL_TEXT);
    lv_obj_align(s_vol_lbl, LV_ALIGN_TOP_MID, 0, 338);

    s_vol_up = lv_obj_create(s_view);
    lv_obj_remove_style_all(s_vol_up);
    lv_obj_set_size(s_vol_up, 50, 40);
    lv_obj_align(s_vol_up, LV_ALIGN_TOP_MID, 90, 330);
    lv_obj_set_style_bg_color(s_vol_up, COL_ITEM_BG, 0);
    lv_obj_set_style_bg_opa(s_vol_up, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_vol_up, 8, 0);
    lv_obj_add_flag(s_vol_up, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_vol_up, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *vup_l = make_label(s_vol_up, LV_SYMBOL_VOLUME_MAX,
                                  &lv_font_montserrat_16, COL_TEXT);
    lv_obj_center(vup_l);
    lv_obj_add_event_cb(s_vol_up, vol_up_cb, LV_EVENT_CLICKED, NULL);

    /* Timer button (bottom) */
    s_timer_btn = lv_obj_create(s_view);
    lv_obj_remove_style_all(s_timer_btn);
    lv_obj_set_size(s_timer_btn, 200, 40);
    lv_obj_align(s_timer_btn, LV_ALIGN_TOP_MID, 0, 388);
    lv_obj_set_style_bg_color(s_timer_btn, COL_ITEM_BG, 0);
    lv_obj_set_style_bg_opa(s_timer_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_timer_btn, COL_ITEM_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_timer_btn, 20, 0);
    lv_obj_add_flag(s_timer_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_timer_btn, LV_OBJ_FLAG_SCROLLABLE);
    s_timer_lbl = make_label(s_timer_btn, "Timer: " LV_SYMBOL_LOOP,
                              &lv_font_montserrat_16, COL_TEXT);
    lv_obj_center(s_timer_lbl);
    lv_obj_add_event_cb(s_timer_btn, timer_cb, LV_EVENT_CLICKED, NULL);
}

/* ── Show / hide ───────────────────────────────────────────────────── */

void ui_noise_show(void)
{
    if (s_view) lv_obj_clear_flag(s_view, LV_OBJ_FLAG_HIDDEN);
}

void ui_noise_hide(void)
{
    if (s_view) lv_obj_add_flag(s_view, LV_OBJ_FLAG_HIDDEN);
}

bool ui_noise_is_visible(void)
{
    return s_view && !lv_obj_has_flag(s_view, LV_OBJ_FLAG_HIDDEN);
}

/* ── Per-tick refresh ──────────────────────────────────────────────── */

void ui_noise_update(void)
{
    if (!ui_noise_is_visible()) return;

    /* Voice highlight (in case API changed it from elsewhere) */
    apply_voice_highlight();

    /* Play/pause button — three states:
     *   idle      → green, ▶
     *   playing   → orange, ⏹
     *   stopping  → dimmed, ▶  (fade-out in progress; user-stop ~1.5 s) */
    bool playing  = noise_player_is_playing();
    bool stopping = noise_player_is_stopping();
    lv_color_t btn_col = !playing ? COL_PLAY_IDLE
                       : stopping  ? COL_PLAY_STOPPING
                                   : COL_PLAY_ACTIVE;
    lv_obj_set_style_bg_color(s_play_btn, btn_col, 0);
    lv_label_set_text(s_play_icon,
        (playing && !stopping) ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);

    /* Volume label */
    char vbuf[16];
    snprintf(vbuf, sizeof(vbuf), "Vol: %u", (unsigned)s_vol_pct);
    lv_label_set_text(s_vol_lbl, vbuf);

    /* Timer label — when playing show countdown, otherwise show preset. */
    char tbuf[32];
    uint16_t preset_min = TIMER_PRESETS[s_timer_idx];
    if (playing && preset_min != 0) {
        uint16_t rem = noise_player_get_timer_remaining_sec();
        if (rem != NOISE_TIMER_INF) {
            unsigned mm = rem / 60u;
            unsigned ss = rem % 60u;
            snprintf(tbuf, sizeof(tbuf), "Timer: %u:%02u", mm, ss);
        } else {
            snprintf(tbuf, sizeof(tbuf), "Timer: " LV_SYMBOL_LOOP);
        }
    } else if (preset_min == 0) {
        snprintf(tbuf, sizeof(tbuf), "Timer: " LV_SYMBOL_LOOP);
    } else {
        snprintf(tbuf, sizeof(tbuf), "Timer: %u min", (unsigned)preset_min);
    }
    lv_label_set_text(s_timer_lbl, tbuf);

    /* Battery (refresh every ~2 s) */
    static int s_batt_tick = 0;
    if (++s_batt_tick >= 20) {
        s_batt_tick = 0;
        amoled_battery_info_t batt;
        if (amoled_get_battery_info(&batt) == ESP_OK) {
            char buf[24];
            const char *icon = batt.percentage > 75 ? LV_SYMBOL_BATTERY_FULL :
                               batt.percentage > 50 ? LV_SYMBOL_BATTERY_3 :
                               batt.percentage > 25 ? LV_SYMBOL_BATTERY_2 :
                               batt.percentage > 10 ? LV_SYMBOL_BATTERY_1 :
                                                      LV_SYMBOL_BATTERY_EMPTY;
            snprintf(buf, sizeof(buf), "%s %d%%%s", icon, batt.percentage,
                     batt.charging ? " " LV_SYMBOL_CHARGE : "");
            lv_label_set_text(s_batt_lbl, buf);
        }
    }
}
