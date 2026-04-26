/*
 * ui_screens.c — Phase 1 placeholder screens.
 *
 * Five screens cycled by BOOT button: status / feed / play / clean / sleep.
 * Each is a single full-screen container with a label; later phases replace
 * the bodies with real UI (sprite, stat bars, food menu, etc.).
 */

#include "ui_screens.h"
#include "esp_log.h"

static const char *TAG = "ui_screens";

static const char *SCREEN_NAMES[SCREEN_COUNT] = {
    "STATUS", "FEED", "PLAY", "CLEAN", "SLEEP",
};

static lv_obj_t *s_screens[SCREEN_COUNT];
static screen_id_t s_current = SCREEN_STATUS;

static lv_obj_t *make_placeholder(lv_obj_t *parent, const char *name, lv_color_t accent)
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

static void build_status_screen(lv_obj_t *scr)
{
    lv_obj_t *greet = lv_label_create(scr);
    lv_label_set_text(greet, "Hello PixelPet");
    lv_obj_set_style_text_color(greet, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(greet, &lv_font_montserrat_20, 0);
    lv_obj_align(greet, LV_ALIGN_CENTER, 0, -40);

    /* Placeholder "egg" — will become animated sprite in Phase 2 */
    lv_obj_t *egg = lv_obj_create(scr);
    lv_obj_remove_style_all(egg);
    lv_obj_set_size(egg, 80, 100);
    lv_obj_set_style_bg_color(egg, lv_color_hex(0xE8D5A0), 0);
    lv_obj_set_style_bg_opa(egg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(egg, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(egg, LV_ALIGN_CENTER, 0, 40);
}

void ui_screens_init(lv_obj_t *parent)
{
    s_screens[SCREEN_STATUS] = make_placeholder(parent, "STATUS", lv_color_hex(0x00FFAA));
    build_status_screen(s_screens[SCREEN_STATUS]);
    s_screens[SCREEN_FEED]   = make_placeholder(parent, "FEED",   lv_color_hex(0xFFAA00));
    s_screens[SCREEN_PLAY]   = make_placeholder(parent, "PLAY",   lv_color_hex(0xFF4488));
    s_screens[SCREEN_CLEAN]  = make_placeholder(parent, "CLEAN",  lv_color_hex(0x44AAFF));
    s_screens[SCREEN_SLEEP]  = make_placeholder(parent, "SLEEP",  lv_color_hex(0x8866FF));

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
