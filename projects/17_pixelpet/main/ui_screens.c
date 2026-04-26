/*
 * ui_screens.c — Phase 3: care buttons on every screen wired to stat_engine.
 *
 * Status screen hosts the live pet sprite and stat bars. Other screens host
 * the action buttons that call back into the stat engine. After every care
 * action, the active screen refreshes immediately so the player sees the
 * change land before decay drifts the bars again.
 */

#include "ui_screens.h"
#include "pet_renderer.h"
#include "stat_engine.h"
#include "pet_save.h"
#include "rtc_manager.h"
#include "minigame_catch.h"
#include "power_manager.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "ui_screens";

static const char *SCREEN_NAMES[SCREEN_COUNT] = {
    "STATUS", "FEED", "PLAY", "CLEAN", "SLEEP",
};

static lv_obj_t *s_screens[SCREEN_COUNT];
static screen_id_t s_current = SCREEN_STATUS;
static pet_state_t *s_pet;

/* Status widgets */
static lv_obj_t *s_lbl_stage;
static lv_obj_t *s_lbl_age;
static lv_obj_t *s_bar_hunger;
static lv_obj_t *s_bar_happy;
static lv_obj_t *s_bar_energy;

/* Sleep screen toggle label */
static lv_obj_t *s_sleep_btn_label;

/* Memorial overlay (shown automatically when stage == STAGE_DEAD) */
static lv_obj_t *s_memorial;
static lv_obj_t *s_memorial_lifespan;

static lv_obj_t *make_screen(lv_obj_t *parent)
{
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

static lv_obj_t *make_title(lv_obj_t *scr, const char *name, lv_color_t accent)
{
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, name);
    lv_obj_set_style_text_color(title, accent, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);
    return title;
}

static lv_obj_t *make_action_btn(lv_obj_t *parent, const char *label,
                                 lv_color_t color, int width, int y_offset,
                                 lv_event_cb_t cb, void *user)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, 64);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, y_offset);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);
    return btn;
}

/* ── Care callbacks ────────────────────────────────────── */

static void care_cb(lv_event_t *e)
{
    care_action_t action = (care_action_t)(uintptr_t)lv_event_get_user_data(e);
    if (!s_pet) return;
    bool ok = stat_engine_apply_care(s_pet, action);
    ESP_LOGI(TAG, "care %s -> %s", care_action_name(action), ok ? "ok" : "noop");
    if (ok) pet_save_request();
    ui_screens_apply_state(s_pet);
    /* Pop back to status so the player sees the result */
    if (action != CARE_SLEEP_TOGGLE && action != CARE_CLEAN_ONE) {
        ui_screens_show(SCREEN_STATUS);
    }
}

/* ── Status screen ─────────────────────────────────────── */

static lv_obj_t *make_stat_bar(lv_obj_t *parent, lv_color_t color, int y_offset, const char *label_text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_LEFT, 24, y_offset - 18);

    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 280, 14);
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

    pet_renderer_init(scr);

    s_bar_energy = make_stat_bar(scr, lv_color_hex(0xFFD166), -16, "energy");
    s_bar_happy  = make_stat_bar(scr, lv_color_hex(0xEF476F), -50, "happy");
    s_bar_hunger = make_stat_bar(scr, lv_color_hex(0x06D6A0), -84, "hunger");
}

/* ── Other screens ─────────────────────────────────────── */

static void build_feed_screen(lv_obj_t *scr)
{
    make_title(scr, "FEED", lv_color_hex(0xFFAA00));
    make_action_btn(scr, "Meal",  lv_color_hex(0xFFD166), 220, -40,
                    care_cb, (void *)(uintptr_t)CARE_FEED_MEAL);
    make_action_btn(scr, "Snack", lv_color_hex(0xFFAFCC), 220,  60,
                    care_cb, (void *)(uintptr_t)CARE_FEED_SNACK);
}

static void minigame_cb(lv_event_t *e)
{
    (void)e;
    minigame_catch_start();
}

static void build_play_screen(lv_obj_t *scr)
{
    make_title(scr, "PLAY", lv_color_hex(0xFF4488));
    make_action_btn(scr, "Quick play",  lv_color_hex(0xEF476F), 220, -40,
                    care_cb, (void *)(uintptr_t)CARE_PLAY);
    make_action_btn(scr, "Catch apples", lv_color_hex(0xFFD166), 220,  60,
                    minigame_cb, NULL);
}

static void build_clean_screen(lv_obj_t *scr)
{
    make_title(scr, "CLEAN", lv_color_hex(0x44AAFF));
    make_action_btn(scr, "Wipe", lv_color_hex(0x06D6A0), 220, -20,
                    care_cb, (void *)(uintptr_t)CARE_CLEAN_ONE);
    make_action_btn(scr, "Medicine", lv_color_hex(0xA2D2FF), 220, 70,
                    care_cb, (void *)(uintptr_t)CARE_MEDICINE);
}

static void power_off_cb(lv_event_t *e)
{
    (void)e;
    if (s_pet) pet_save_commit(s_pet);
    power_manager_power_off();
}

static void rebirth_cb(lv_event_t *e)
{
    (void)e;
    if (!s_pet) return;
    pet_save_clear();
    pet_state_init_new(s_pet);
    s_pet->hatched_unix     = rtc_manager_now_unix();
    s_pet->last_update_unix = s_pet->hatched_unix;
    pet_save_commit(s_pet);
    ui_screens_apply_state(s_pet);
    ui_screens_show(SCREEN_STATUS);
}

static void build_memorial(lv_obj_t *parent)
{
    s_memorial = lv_obj_create(parent);
    lv_obj_remove_style_all(s_memorial);
    lv_obj_set_size(s_memorial, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_memorial, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(s_memorial, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_memorial, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_memorial, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *rip = lv_label_create(s_memorial);
    lv_label_set_text(rip, "R.I.P.");
    lv_obj_set_style_text_color(rip, lv_color_hex(0xFFE066), 0);
    lv_obj_set_style_text_font(rip, &lv_font_montserrat_28, 0);
    lv_obj_align(rip, LV_ALIGN_TOP_MID, 0, 60);

    s_memorial_lifespan = lv_label_create(s_memorial);
    lv_label_set_text(s_memorial_lifespan, "lived 0 days");
    lv_obj_set_style_text_color(s_memorial_lifespan, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(s_memorial_lifespan, &lv_font_montserrat_20, 0);
    lv_obj_align(s_memorial_lifespan, LV_ALIGN_TOP_MID, 0, 110);

    lv_obj_t *halo = lv_obj_create(s_memorial);
    lv_obj_remove_style_all(halo);
    lv_obj_set_size(halo, 80, 12);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(halo, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(halo, lv_color_hex(0xFFE066), 0);
    lv_obj_set_style_border_width(halo, 4, 0);
    lv_obj_align(halo, LV_ALIGN_CENTER, 0, -20);

    make_action_btn(s_memorial, "Hatch new pet", lv_color_hex(0x06D6A0),
                    240, 80, rebirth_cb, NULL);
}

static void build_sleep_screen(lv_obj_t *scr)
{
    make_title(scr, "SLEEP", lv_color_hex(0x8866FF));
    lv_obj_t *btn = make_action_btn(scr, "Tuck in / Wake up",
                                    lv_color_hex(0xCDB4DB), 280, -40,
                                    care_cb, (void *)(uintptr_t)CARE_SLEEP_TOGGLE);
    s_sleep_btn_label = lv_obj_get_child(btn, 0);

    make_action_btn(scr, "Power off", lv_color_hex(0xEF476F), 220, 60,
                    power_off_cb, NULL);
}

/* ── Public API ────────────────────────────────────────── */

void ui_screens_init(lv_obj_t *parent, pet_state_t *pet)
{
    s_pet = pet;

    s_screens[SCREEN_STATUS] = make_screen(parent);
    build_status_screen(s_screens[SCREEN_STATUS]);

    s_screens[SCREEN_FEED]  = make_screen(parent); build_feed_screen(s_screens[SCREEN_FEED]);
    s_screens[SCREEN_PLAY]  = make_screen(parent); build_play_screen(s_screens[SCREEN_PLAY]);
    s_screens[SCREEN_CLEAN] = make_screen(parent); build_clean_screen(s_screens[SCREEN_CLEAN]);
    s_screens[SCREEN_SLEEP] = make_screen(parent); build_sleep_screen(s_screens[SCREEN_SLEEP]);

    build_memorial(parent);

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
        int64_t age_s = rtc_manager_now_unix() - p->hatched_unix;
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

    if (s_sleep_btn_label) {
        lv_label_set_text(s_sleep_btn_label, p->is_sleeping ? "Wake up" : "Tuck in");
    }

    /* Memorial overlay tracks the dead-or-alive state automatically. */
    if (s_memorial) {
        if (p->stage == STAGE_DEAD) {
            int64_t age_s = rtc_manager_now_unix() - p->hatched_unix;
            if (age_s < 0) age_s = 0;
            char buf[40];
            if (age_s < 3600)        snprintf(buf, sizeof(buf), "lived %llds", age_s);
            else if (age_s < 86400)  snprintf(buf, sizeof(buf), "lived %lldh", age_s / 3600);
            else                     snprintf(buf, sizeof(buf), "lived %lldd", age_s / 86400);
            lv_label_set_text(s_memorial_lifespan, buf);
            lv_obj_clear_flag(s_memorial, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_memorial);
        } else {
            lv_obj_add_flag(s_memorial, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
