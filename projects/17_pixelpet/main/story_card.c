/*
 * story_card.c — full-screen modal story beat overlay.
 *
 * One reusable card with title + body + optional sprite + tap-to-dismiss
 * footer. Stage transitions, adult-form reveals and milestone callouts
 * all flow through this single widget so the visual language stays
 * consistent and the RAM cost stays at one container.
 */

#include "story_card.h"
#include "asset_loader.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "story";

static lv_obj_t *s_root;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_sprite;
static lv_obj_t *s_footer;
static bool      s_visible;

static void on_tap(lv_event_t *e)
{
    (void)e;
    story_card_hide();
}

void story_card_init(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x0A0A14), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, on_tap, LV_EVENT_PRESSED, NULL);

    s_title = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(s_title, "");

    s_sprite = lv_img_create(s_root);
    lv_img_set_antialias(s_sprite, false);
    lv_obj_align(s_sprite, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_flag(s_sprite, LV_OBJ_FLAG_HIDDEN);

    s_body = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_body, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_body, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_body, 320);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_body, LV_ALIGN_CENTER, 0, 100);
    lv_label_set_text(s_body, "");

    s_footer = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_footer, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_footer, lv_color_hex(0x808080), 0);
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_label_set_text(s_footer, "tap to continue");
}

void story_card_show(const char *title, const char *body,
                     const char *sprite_name, uint32_t accent_hex)
{
    if (!s_root) return;
    lv_label_set_text(s_title, title ? title : "");
    lv_obj_set_style_text_color(s_title, lv_color_hex(accent_hex), 0);
    lv_label_set_text(s_body, body ? body : "");

    if (sprite_name) {
        const asset_anim_t *a = asset_get_by_name(sprite_name);
        if (a) {
            lv_img_set_src(s_sprite, a->frames[0]);
            lv_obj_set_size(s_sprite, a->width, a->height);
            lv_obj_align(s_sprite, LV_ALIGN_CENTER, 0, -20);
            lv_obj_clear_flag(s_sprite, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_sprite, LV_OBJ_FLAG_HIDDEN);
            ESP_LOGW(TAG, "story sprite missing: %s", sprite_name);
        }
    } else {
        lv_obj_add_flag(s_sprite, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);
    s_visible = true;
}

bool story_card_is_visible(void) { return s_visible; }

void story_card_hide(void)
{
    if (!s_root) return;
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
}

/* ── Convenience presets ──────────────────────────────────── */

static const char *STAGE_TIPS[STAGE_COUNT] = {
    [STAGE_EGG]    = "an egg has appeared\nkeep it warm",
    [STAGE_BABY]   = "a baby! feed often\nand watch it sleep",
    [STAGE_CHILD]  = "growing curious\nplay keeps them happy",
    [STAGE_TEEN]   = "moody and easily bored\nplay AND discipline",
    [STAGE_ADULT]  = "all grown up\nthe form depends on care",
    [STAGE_SENIOR] = "calm and slow now\nlet them rest",
    [STAGE_DEAD]   = "",
};

void story_card_show_stageup(const char *pet_name, pet_stage_t new_stage)
{
    char title[40];
    const char *stage_str = pet_stage_name(new_stage);
    snprintf(title, sizeof(title), "%s is now\n%s",
             (pet_name && pet_name[0]) ? pet_name : "your pet",
             stage_str);
    const char *tip = (new_stage < STAGE_COUNT) ? STAGE_TIPS[new_stage] : "";
    story_card_show(title, tip, NULL, 0xFFD166);
}

static const char *ADULT_BLURB[] = {
    [ADULT_HAPPY]   = "lots of love + discipline\ngrew them HAPPY",
    [ADULT_LAZY]    = "low energy and care\ngrew them LAZY",
    [ADULT_NAUGHTY] = "not enough discipline\ngrew them NAUGHTY",
    [ADULT_SICK]    = "neglected health\ngrew them SICKLY",
};

static const char *ADULT_NAMES[] = {
    [ADULT_HAPPY]   = "HAPPY",
    [ADULT_LAZY]    = "LAZY",
    [ADULT_NAUGHTY] = "NAUGHTY",
    [ADULT_SICK]    = "SICKLY",
};

void story_card_show_adult(const char *pet_name, pet_adult_form_t form)
{
    if ((int)form < 0 || (int)form >= (int)(sizeof(ADULT_NAMES) / sizeof(ADULT_NAMES[0]))) {
        return;
    }
    char title[40];
    snprintf(title, sizeof(title), "%s grew up\n%s",
             (pet_name && pet_name[0]) ? pet_name : "your pet",
             ADULT_NAMES[form]);
    story_card_show(title, ADULT_BLURB[form], NULL, 0x00FFAA);
}
