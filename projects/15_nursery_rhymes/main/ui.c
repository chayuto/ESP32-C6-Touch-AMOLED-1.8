/*
 * ui.c — LVGL touch UI for nursery rhyme player.
 *
 * Two views:
 *   1. Song List — scrollable list of 20 songs, tap to play
 *   2. Now Playing — song title, progress bar, stop button
 *
 * Child lock overlay shown when locked (long-press BOOT button).
 * Screen-off mode blanks display for power saving / no screen time.
 */

#include "ui.h"
#include "audio_player.h"
#include "song_data.h"
#include "amoled.h"

#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui";

/* Screen dimensions */
#define SCR_W  AMOLED_LCD_H_RES   /* 368 */
#define SCR_H  AMOLED_LCD_V_RES   /* 448 */

/* Colors */
#define COL_BG           lv_color_make(10, 10, 30)
#define COL_HEADER_BG    lv_color_make(30, 20, 60)
#define COL_ITEM_BG      lv_color_make(25, 25, 55)
#define COL_ITEM_PRESSED lv_color_make(60, 40, 100)
#define COL_ACCENT       lv_color_make(120, 80, 220)
#define COL_TEXT         lv_color_white()
#define COL_TEXT_DIM     lv_color_make(160, 160, 180)
#define COL_PLAYING_BG   lv_color_make(15, 10, 40)
#define COL_STOP_BTN     lv_color_make(200, 60, 60)
#define COL_LOCK_BG      lv_color_make(0, 0, 0)
#define COL_LOCK_TEXT    lv_color_make(100, 100, 120)
#define COL_NOTE         lv_color_make(255, 200, 80)

/* UI objects */
static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_song_list    = NULL;  /* scrollable song list container */
static lv_obj_t *s_now_playing  = NULL;  /* now-playing view */
static lv_obj_t *s_lock_overlay = NULL;  /* child lock overlay */

/* Now-playing sub-widgets */
static lv_obj_t *s_np_title     = NULL;
static lv_obj_t *s_np_lyrics    = NULL;
static lv_obj_t *s_np_progress  = NULL;
static lv_obj_t *s_np_note_icon = NULL;
static lv_obj_t *s_np_stop_btn  = NULL;
static lv_obj_t *s_np_back_btn  = NULL;
static lv_obj_t *s_np_vol_label = NULL;
static lv_obj_t *s_np_vol_up    = NULL;
static lv_obj_t *s_np_vol_dn    = NULL;
static lv_obj_t *s_np_mode_btn  = NULL;
static lv_obj_t *s_np_mode_lbl  = NULL;
static lv_obj_t *s_np_batt_lbl  = NULL;

/* Lock overlay widgets */
static lv_obj_t *s_lock_label   = NULL;

/* State */
static bool s_child_locked = false;
static bool s_screen_off   = false;
static int  s_last_playing = -1;

/* Song list item buttons */
static lv_obj_t *s_song_btns[40];

/* ── Helpers ──────────────────────────────────────────── */

static lv_obj_t *create_label(lv_obj_t *parent, const char *text,
                               const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    return lbl;
}

/* ── Song List View ───────────────────────────────────── */

static void song_btn_cb(lv_event_t *e)
{
    if (s_child_locked) return;

    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Song selected: %d — %s", idx, g_songs[idx].title);
    audio_player_play(idx);
}

static void build_song_list(lv_obj_t *parent)
{
    s_song_list = lv_obj_create(parent);
    lv_obj_remove_style_all(s_song_list);
    lv_obj_set_size(s_song_list, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_song_list, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_song_list, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_song_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_song_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(s_song_list, 0, 0);
    lv_obj_set_style_pad_row(s_song_list, 4, 0);
    lv_obj_set_scrollbar_mode(s_song_list, LV_SCROLLBAR_MODE_OFF);

    /* Header */
    lv_obj_t *header = lv_obj_create(s_song_list);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, SCR_W, 52);
    lv_obj_set_style_bg_color(header, COL_HEADER_BG, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(header, 16, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = create_label(header, LV_SYMBOL_AUDIO "  Nursery Rhymes",
                                    &lv_font_montserrat_18, COL_NOTE);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    /* Song buttons */
    for (int i = 0; i < g_song_count && i < 40; i++) {
        lv_obj_t *btn = lv_obj_create(s_song_list);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, SCR_W - 16, 56);
        lv_obj_set_style_bg_color(btn, COL_ITEM_BG, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, COL_ITEM_PRESSED, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_pad_left(btn, 14, 0);
        lv_obj_set_style_pad_right(btn, 10, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        /* Number */
        char num_str[4];
        snprintf(num_str, sizeof(num_str), "%d", i + 1);
        lv_obj_t *num = create_label(btn, num_str, &lv_font_montserrat_14, COL_ACCENT);
        lv_obj_align(num, LV_ALIGN_LEFT_MID, 0, 0);

        /* Title */
        lv_obj_t *name = create_label(btn, g_songs[i].title,
                                       &lv_font_montserrat_16, COL_TEXT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 28, -8);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, SCR_W - 80);

        /* Subtitle (first line of lyrics) */
        lv_obj_t *sub = create_label(btn, g_songs[i].lyrics_short,
                                      &lv_font_montserrat_12, COL_TEXT_DIM);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 28, 10);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
        lv_obj_set_width(sub, SCR_W - 80);

        /* Play icon on right */
        lv_obj_t *play_icon = create_label(btn, LV_SYMBOL_PLAY,
                                            &lv_font_montserrat_16, COL_ACCENT);
        lv_obj_align(play_icon, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_add_event_cb(btn, song_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_song_btns[i] = btn;
    }
}

/* ── Now Playing View ─────────────────────────────────── */

static void stop_btn_cb(lv_event_t *e)
{
    (void)e;
    audio_player_stop();
}

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    audio_player_stop();
}

static void mode_btn_cb(lv_event_t *e)
{
    (void)e;
    audio_player_cycle_mode();
}

static void vol_up_cb(lv_event_t *e)
{
    (void)e;
    int v = audio_player_get_volume() + 10;
    if (v > 100) v = 100;
    audio_player_set_volume(v);
}

static void vol_dn_cb(lv_event_t *e)
{
    (void)e;
    int v = audio_player_get_volume() - 10;
    if (v < 0) v = 0;
    audio_player_set_volume(v);
}

static void build_now_playing(lv_obj_t *parent)
{
    s_now_playing = lv_obj_create(parent);
    lv_obj_remove_style_all(s_now_playing);
    lv_obj_set_size(s_now_playing, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_now_playing, COL_PLAYING_BG, 0);
    lv_obj_set_style_bg_opa(s_now_playing, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_now_playing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_now_playing, LV_OBJ_FLAG_SCROLLABLE);

    /* Back arrow (top-left) */
    s_np_back_btn = lv_obj_create(s_now_playing);
    lv_obj_remove_style_all(s_np_back_btn);
    lv_obj_set_size(s_np_back_btn, 50, 40);
    lv_obj_set_pos(s_np_back_btn, 8, 8);
    lv_obj_add_flag(s_np_back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *back_lbl = create_label(s_np_back_btn, LV_SYMBOL_LEFT,
                                       &lv_font_montserrat_18, COL_TEXT);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(s_np_back_btn, back_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Musical note icon */
    s_np_note_icon = create_label(s_now_playing, LV_SYMBOL_AUDIO,
                                   &lv_font_montserrat_40, COL_NOTE);
    lv_obj_align(s_np_note_icon, LV_ALIGN_TOP_MID, 0, 70);

    /* Song title */
    s_np_title = create_label(s_now_playing, "Song Title",
                               &lv_font_montserrat_22, COL_TEXT);
    lv_obj_align(s_np_title, LV_ALIGN_TOP_MID, 0, 140);
    lv_label_set_long_mode(s_np_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_np_title, SCR_W - 40);
    lv_obj_set_style_text_align(s_np_title, LV_TEXT_ALIGN_CENTER, 0);

    /* Lyrics / subtitle */
    s_np_lyrics = create_label(s_now_playing, "",
                                &lv_font_montserrat_14, COL_TEXT_DIM);
    lv_obj_align(s_np_lyrics, LV_ALIGN_TOP_MID, 0, 175);
    lv_label_set_long_mode(s_np_lyrics, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_np_lyrics, SCR_W - 40);
    lv_obj_set_style_text_align(s_np_lyrics, LV_TEXT_ALIGN_CENTER, 0);

    /* Progress bar */
    s_np_progress = lv_bar_create(s_now_playing);
    lv_obj_set_size(s_np_progress, SCR_W - 60, 12);
    lv_obj_align(s_np_progress, LV_ALIGN_TOP_MID, 0, 220);
    lv_bar_set_range(s_np_progress, 0, 100);
    lv_bar_set_value(s_np_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_np_progress, lv_color_make(40, 40, 60), 0);
    lv_obj_set_style_bg_color(s_np_progress, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_np_progress, 6, 0);
    lv_obj_set_style_radius(s_np_progress, 6, LV_PART_INDICATOR);

    /* Battery gauge (top-right) */
    s_np_batt_lbl = create_label(s_now_playing, LV_SYMBOL_BATTERY_FULL " 100%",
                                  &lv_font_montserrat_12, COL_TEXT_DIM);
    lv_obj_align(s_np_batt_lbl, LV_ALIGN_TOP_RIGHT, -12, 14);

    /* Play mode button (below progress bar) */
    s_np_mode_btn = lv_obj_create(s_now_playing);
    lv_obj_remove_style_all(s_np_mode_btn);
    lv_obj_set_size(s_np_mode_btn, 160, 34);
    lv_obj_align(s_np_mode_btn, LV_ALIGN_TOP_MID, 0, 240);
    lv_obj_set_style_bg_color(s_np_mode_btn, COL_ITEM_BG, 0);
    lv_obj_set_style_bg_opa(s_np_mode_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_np_mode_btn, COL_ITEM_PRESSED, LV_STATE_PRESSED);
    lv_obj_set_style_radius(s_np_mode_btn, 17, 0);
    lv_obj_add_flag(s_np_mode_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_np_mode_btn, LV_OBJ_FLAG_SCROLLABLE);
    s_np_mode_lbl = create_label(s_np_mode_btn, LV_SYMBOL_RIGHT " Play Once",
                                  &lv_font_montserrat_14, COL_TEXT);
    lv_obj_center(s_np_mode_lbl);
    lv_obj_add_event_cb(s_np_mode_btn, mode_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Stop button (large, centered) */
    s_np_stop_btn = lv_obj_create(s_now_playing);
    lv_obj_remove_style_all(s_np_stop_btn);
    lv_obj_set_size(s_np_stop_btn, 80, 80);
    lv_obj_align(s_np_stop_btn, LV_ALIGN_TOP_MID, 0, 290);
    lv_obj_set_style_bg_color(s_np_stop_btn, COL_STOP_BTN, 0);
    lv_obj_set_style_bg_opa(s_np_stop_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_np_stop_btn, 40, 0);
    lv_obj_add_flag(s_np_stop_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_np_stop_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *stop_lbl = create_label(s_np_stop_btn, LV_SYMBOL_STOP,
                                       &lv_font_montserrat_28, COL_TEXT);
    lv_obj_center(stop_lbl);
    lv_obj_add_event_cb(s_np_stop_btn, stop_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Volume controls */
    s_np_vol_dn = lv_obj_create(s_now_playing);
    lv_obj_remove_style_all(s_np_vol_dn);
    lv_obj_set_size(s_np_vol_dn, 50, 40);
    lv_obj_align(s_np_vol_dn, LV_ALIGN_TOP_MID, -80, 395);
    lv_obj_set_style_bg_color(s_np_vol_dn, COL_ITEM_BG, 0);
    lv_obj_set_style_bg_opa(s_np_vol_dn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_np_vol_dn, 8, 0);
    lv_obj_add_flag(s_np_vol_dn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_np_vol_dn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *vdn = create_label(s_np_vol_dn, LV_SYMBOL_VOLUME_MID,
                                  &lv_font_montserrat_16, COL_TEXT);
    lv_obj_center(vdn);
    lv_obj_add_event_cb(s_np_vol_dn, vol_dn_cb, LV_EVENT_CLICKED, NULL);

    s_np_vol_label = create_label(s_now_playing, "Vol: 70",
                                   &lv_font_montserrat_14, COL_TEXT_DIM);
    lv_obj_align(s_np_vol_label, LV_ALIGN_TOP_MID, 0, 403);

    s_np_vol_up = lv_obj_create(s_now_playing);
    lv_obj_remove_style_all(s_np_vol_up);
    lv_obj_set_size(s_np_vol_up, 50, 40);
    lv_obj_align(s_np_vol_up, LV_ALIGN_TOP_MID, 80, 395);
    lv_obj_set_style_bg_color(s_np_vol_up, COL_ITEM_BG, 0);
    lv_obj_set_style_bg_opa(s_np_vol_up, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_np_vol_up, 8, 0);
    lv_obj_add_flag(s_np_vol_up, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_np_vol_up, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *vup = create_label(s_np_vol_up, LV_SYMBOL_VOLUME_MAX,
                                  &lv_font_montserrat_16, COL_TEXT);
    lv_obj_center(vup);
    lv_obj_add_event_cb(s_np_vol_up, vol_up_cb, LV_EVENT_CLICKED, NULL);
}

/* ── Child Lock Overlay ───────────────────────────────── */

static void build_lock_overlay(lv_obj_t *parent)
{
    s_lock_overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_lock_overlay);
    lv_obj_set_size(s_lock_overlay, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_lock_overlay, COL_LOCK_BG, 0);
    lv_obj_set_style_bg_opa(s_lock_overlay, LV_OPA_80, 0);
    lv_obj_add_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lock_overlay, LV_OBJ_FLAG_CLICKABLE); /* absorb touches */
    lv_obj_clear_flag(s_lock_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lock_icon = create_label(s_lock_overlay, LV_SYMBOL_EYE_CLOSE,
                                        &lv_font_montserrat_40, COL_LOCK_TEXT);
    lv_obj_align(lock_icon, LV_ALIGN_CENTER, 0, -30);

    s_lock_label = create_label(s_lock_overlay, "Child Lock Active\nLong-press button to unlock",
                                 &lv_font_montserrat_16, COL_LOCK_TEXT);
    lv_obj_set_style_text_align(s_lock_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_lock_label, LV_ALIGN_CENTER, 0, 30);
}

/* ── Public API ───────────────────────────────────────── */

void ui_init(lv_obj_t *screen)
{
    s_screen = screen;
    lv_obj_set_style_bg_color(screen, COL_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    build_song_list(screen);
    build_now_playing(screen);
    build_lock_overlay(screen);

    ESP_LOGI(TAG, "UI initialized with %d songs", g_song_count);
}

void ui_update(void)
{
    bool playing = audio_player_is_playing();
    int cur = audio_player_current_song();

    /* Transition to now-playing when song starts */
    if (playing && cur >= 0 && s_last_playing != cur) {
        lv_obj_add_flag(s_song_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_now_playing, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(s_np_title, g_songs[cur].title);
        lv_label_set_text(s_np_lyrics, g_songs[cur].lyrics_short);
        s_last_playing = cur;
    }

    /* Update progress bar */
    if (playing) {
        lv_bar_set_value(s_np_progress, audio_player_progress(), LV_ANIM_ON);
    }

    /* Transition back to song list when song ends (only in play-once mode) */
    if (!playing && s_last_playing >= 0) {
        play_mode_t m = audio_player_get_mode();
        if (m == PLAY_MODE_OFF) {
            /* No auto-advance — return to list */
            lv_obj_clear_flag(s_song_list, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_now_playing, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(s_np_progress, 0, LV_ANIM_OFF);
            s_last_playing = -1;
        }
        /* Otherwise stay on now-playing — next song will start shortly */
    }

    /* Update volume label */
    static int s_last_vol = -1;
    int vol = audio_player_get_volume();
    if (vol != s_last_vol) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Vol: %d", vol);
        lv_label_set_text(s_np_vol_label, buf);
        s_last_vol = vol;
    }

    /* Update play mode label */
    static play_mode_t s_last_mode = PLAY_MODE_COUNT;
    play_mode_t mode = audio_player_get_mode();
    if (mode != s_last_mode) {
        static const char *mode_labels[] = {
            LV_SYMBOL_RIGHT " Play Once",
            LV_SYMBOL_LOOP " Loop All",
            LV_SYMBOL_REFRESH " Loop One",
            LV_SYMBOL_SHUFFLE " Shuffle",
        };
        lv_label_set_text(s_np_mode_lbl, mode_labels[mode]);
        s_last_mode = mode;
    }

    /* Update battery gauge (every ~2 seconds via counter) */
    static int s_batt_tick = 0;
    if (++s_batt_tick >= 20) {  /* 20 × 100ms timer = 2s */
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
            lv_label_set_text(s_np_batt_lbl, buf);
        }
    }
}

void ui_set_child_lock(bool locked)
{
    s_child_locked = locked;
    if (locked) {
        lv_obj_clear_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_lock_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "Child lock: %s", locked ? "ON" : "OFF");
}

bool ui_get_child_lock(void)
{
    return s_child_locked;
}

void ui_set_screen_off(bool off)
{
    s_screen_off = off;
    amoled_display_on_off(!off);
    if (off) {
        amoled_set_brightness(0);
    } else {
        amoled_set_brightness(180);
    }
    ESP_LOGI(TAG, "Screen: %s", off ? "OFF" : "ON");
}

bool ui_get_screen_off(void)
{
    return s_screen_off;
}
