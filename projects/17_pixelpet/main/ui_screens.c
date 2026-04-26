/*
 * ui_screens.c — Phase 2: status screen wired to pet_renderer + stat bars.
 *
 * Five screens cycled by BOOT button. Status hosts the live pet sprite and
 * three stat bars (hunger / happy / energy). Other screens stay placeholders
 * until phases 3-5.
 */

#include "ui_screens.h"
#include "pet_renderer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>

static const char *TAG = "ui_screens";

static const char *SCREEN_NAMES[SCREEN_COUNT] = {
    "STATUS", "FEED", "PLAY", "CLEAN", "SLEEP",
};

static lv_obj_t *s_screens[SCREEN_COUNT];
static screen_id_t s_current = SCREEN_STATUS;

/* Status screen widgets */
static lv_obj_t *s_lbl_stage;
static lv_obj_t *s_lbl_age;
static lv_obj_t *s_bar_hunger;
static lv_obj_t *s_bar_happy;
static lv_obj_t *s_bar_energy;

static lv_obj_t *make_stub_screen(lv_obj_t *parent, const char *name, lv_color_t accent)
{
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, name);
    lv_obj_set_style_text_color(title, accent, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "press BOOT for next");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -24);

    return scr;
}

static lv_obj_t *make_stat_bar(lv_obj_t *parent, lv_color_t color, int y_offset)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 280, 16);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, y_offset);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);
    return bar;
}

static void build_status_screen(lv_obj_t *scr)
{
    /* Top: stage name + age */
    s_lbl_stage = lv_label_create(scr);
    lv_label_set_text(s_lbl_stage, "EGG");
    lv_obj_set_style_text_color(s_lbl_stage, lv_color_hex(0x00FFAA), 0);
    lv_obj_set_style_text_font(s_lbl_stage, &lv_font_montserrat_20, 0);
    lv_obj_align(s_lbl_stage, LV_ALIGN_TOP_MID, 0, 12);

    s_lbl_age = lv_label_create(scr);
    lv_label_set_text(s_lbl_age, "age 0s");
    lv_obj_set_style_text_color(s_lbl_age, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(s_lbl_age, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_age, LV_ALIGN_TOP_MID, 0, 38);

    /* Pet sprite */
    pet_renderer_init(scr);

    /* Stat bars */
    s_bar_energy = make_stat_bar(scr, lv_color_hex(0xFFD166), -16);
    s_bar_happy  = make_stat_bar(scr, lv_color_hex(0xEF476F), -40);
    s_bar_hunger = make_stat_bar(scr, lv_color_hex(0x06D6A0), -64);
}

void ui_screens_init(lv_obj_t *parent)
{
    s_screens[SCREEN_STATUS] = make_stub_screen(parent, "", lv_color_hex(0x00FFAA));
    /* Replace title — status screen uses its own header */
    lv_obj_clean(s_screens[SCREEN_STATUS]);
    build_status_screen(s_screens[SCREEN_STATUS]);

    s_screens[SCREEN_FEED]  = make_stub_screen(parent, "FEED",  lv_color_hex(0xFFAA00));
    s_screens[SCREEN_PLAY]  = make_stub_screen(parent, "PLAY",  lv_color_hex(0xFF4488));
    s_screens[SCREEN_CLEAN] = make_stub_screen(parent, "CLEAN", lv_color_hex(0x44AAFF));
    s_screens[SCREEN_SLEEP] = make_stub_screen(parent, "SLEEP", lv_color_hex(0x8866FF));

    for (int i = 0; i < SCREEN_COUNT; i++) {
        lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_HIDDEN);
    }
    ui_screens_show(SCREEN_STATUS);
}

void ui_screens_show(screen_id_t id)
{
    if (id >= SCREEN_COUNT) return;
    lv_obj_add_flag(s_screens[s_current], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_screens[id], LV_OBJ_FLAG_HIDDEN);
    s_current = id;
    ESP_LOGI(TAG, "screen → %s", SCREEN_NAMES[id]);
}

void ui_screens_next(void)
{
    ui_screens_show((s_current + 1) % SCREEN_COUNT);
}

screen_id_t ui_screens_current(void)
{
    return s_current;
}

void ui_screens_apply_state(const pet_state_t *p)
{
    pet_renderer_set_state(p);

    if (s_lbl_stage) lv_label_set_text(s_lbl_stage, pet_stage_name(p->stage));

    if (s_lbl_age) {
        int64_t now_us = esp_timer_get_time();
        int64_t age_s  = (now_us - p->hatched_unix) / 1000000;
        if (age_s < 0) age_s = 0;
        char buf[32];
        if (age_s < 60)            snprintf(buf, sizeof(buf), "age %llds", age_s);
        else if (age_s < 3600)     snprintf(buf, sizeof(buf), "age %lldm", age_s / 60);
        else if (age_s < 86400)    snprintf(buf, sizeof(buf), "age %lldh", age_s / 3600);
        else                       snprintf(buf, sizeof(buf), "age %lldd", age_s / 86400);
        lv_label_set_text(s_lbl_age, buf);
    }

    if (s_bar_hunger) lv_bar_set_value(s_bar_hunger, p->hunger, LV_ANIM_OFF);
    if (s_bar_happy)  lv_bar_set_value(s_bar_happy,  p->happy,  LV_ANIM_OFF);
    if (s_bar_energy) lv_bar_set_value(s_bar_energy, p->energy, LV_ANIM_OFF);
}
