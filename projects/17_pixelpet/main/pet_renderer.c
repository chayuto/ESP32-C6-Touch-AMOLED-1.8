/*
 * pet_renderer.c — procedural pet sprite (Phase 2 placeholder).
 *
 * The pet is composed from LVGL primitives: rounded body, eyes, mouth, and
 * a few accessory layers (poop, Z when sleeping, halo when dead). Stage and
 * mood drive colour, eye shape, mouth curvature, and bob amplitude.
 *
 * Real pixel-art bitmaps land in Phase 7. The composition API stays the same
 * — `pet_renderer_set_state` will switch from drawing primitives to
 * lv_image_set_src once the sprite sheets are in flash.
 */

#include "pet_renderer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

static const char *TAG = "pet_renderer";

static lv_obj_t *s_root;       /* container, holds everything */
static lv_obj_t *s_body;
static lv_obj_t *s_eye_l;
static lv_obj_t *s_eye_r;
static lv_obj_t *s_mouth;
static lv_obj_t *s_zzz;        /* sleep indicator */
static lv_obj_t *s_halo;       /* death indicator */
static lv_obj_t *s_poops[3];

static pet_stage_t s_stage = STAGE_EGG;
static pet_mood_t  s_mood  = MOOD_NEUTRAL;
static int         s_frame = 0;

/* Colour palette, deliberately retro and chunky */
static lv_color_t stage_body_color(pet_stage_t s, pet_mood_t m)
{
    if (m == MOOD_SICK) return lv_color_hex(0x8FBC8F);   /* pale green */
    if (m == MOOD_DEAD) return lv_color_hex(0x404040);   /* grey */
    switch (s) {
    case STAGE_EGG:    return lv_color_hex(0xE8D5A0);
    case STAGE_BABY:   return lv_color_hex(0xFFC8DD);   /* pink blob */
    case STAGE_CHILD:  return lv_color_hex(0xFFAFCC);
    case STAGE_TEEN:   return lv_color_hex(0xCDB4DB);   /* lavender */
    case STAGE_ADULT:  return lv_color_hex(0xA2D2FF);
    case STAGE_SENIOR: return lv_color_hex(0xBDE0FE);
    case STAGE_DEAD:   return lv_color_hex(0x404040);
    default:           return lv_color_hex(0xFFFFFF);
    }
}

static int stage_body_size(pet_stage_t s)
{
    switch (s) {
    case STAGE_EGG:    return 90;
    case STAGE_BABY:   return 80;
    case STAGE_CHILD:  return 100;
    case STAGE_TEEN:   return 120;
    case STAGE_ADULT:  return 140;
    case STAGE_SENIOR: return 130;
    case STAGE_DEAD:   return 100;
    default:           return 100;
    }
}

void pet_renderer_init(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, 200, 200);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_root, LV_ALIGN_CENTER, 0, 0);

    s_body = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_body);
    lv_obj_set_size(s_body, 100, 100);
    lv_obj_set_style_radius(s_body, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_COVER, 0);
    lv_obj_align(s_body, LV_ALIGN_CENTER, 0, 0);

    s_eye_l = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_eye_l);
    lv_obj_set_size(s_eye_l, 10, 14);
    lv_obj_set_style_bg_color(s_eye_l, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_eye_l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_eye_l, 5, 0);
    lv_obj_align(s_eye_l, LV_ALIGN_CENTER, -18, -10);

    s_eye_r = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_eye_r);
    lv_obj_set_size(s_eye_r, 10, 14);
    lv_obj_set_style_bg_color(s_eye_r, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_eye_r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_eye_r, 5, 0);
    lv_obj_align(s_eye_r, LV_ALIGN_CENTER, 18, -10);

    s_mouth = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_mouth);
    lv_obj_set_size(s_mouth, 20, 4);
    lv_obj_set_style_bg_color(s_mouth, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_mouth, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_mouth, 2, 0);
    lv_obj_align(s_mouth, LV_ALIGN_CENTER, 0, 14);

    s_zzz = lv_label_create(s_root);
    lv_label_set_text(s_zzz, "z z Z");
    lv_obj_set_style_text_color(s_zzz, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(s_zzz, &lv_font_montserrat_20, 0);
    lv_obj_align(s_zzz, LV_ALIGN_TOP_RIGHT, -10, 0);
    lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);

    s_halo = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_halo);
    lv_obj_set_size(s_halo, 60, 8);
    lv_obj_set_style_radius(s_halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_halo, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_halo, lv_color_hex(0xFFE066), 0);
    lv_obj_set_style_border_width(s_halo, 3, 0);
    lv_obj_align(s_halo, LV_ALIGN_TOP_MID, 0, -20);
    lv_obj_add_flag(s_halo, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 3; i++) {
        s_poops[i] = lv_label_create(parent); /* below pet, on screen */
        lv_label_set_text(s_poops[i], "*");
        lv_obj_set_style_text_color(s_poops[i], lv_color_hex(0x6B4423), 0);
        lv_obj_set_style_text_font(s_poops[i], &lv_font_montserrat_28, 0);
        lv_obj_align(s_poops[i], LV_ALIGN_BOTTOM_LEFT, 30 + i * 50, -150);
        lv_obj_add_flag(s_poops[i], LV_OBJ_FLAG_HIDDEN);
    }

    ESP_LOGI(TAG, "pet renderer ready");
}

pet_mood_t pet_renderer_derive_mood(const pet_state_t *p)
{
    if (p->stage == STAGE_DEAD)        return MOOD_DEAD;
    if (p->is_sleeping)                return MOOD_SLEEPING;
    if (p->health < 30 || p->clean < 20) return MOOD_SICK;
    if (p->hunger < 25)                return MOOD_HUNGRY;
    if (p->energy < 20)                return MOOD_TIRED;
    if (p->happy  < 30)                return MOOD_SAD;
    if (p->happy  > 80 && p->hunger > 60) return MOOD_HAPPY;
    return MOOD_NEUTRAL;
}

static void apply_mood_to_face(pet_mood_t m)
{
    /* Eye shapes by mood */
    switch (m) {
    case MOOD_SLEEPING:
    case MOOD_TIRED:
        lv_obj_set_size(s_eye_l, 12, 3);
        lv_obj_set_size(s_eye_r, 12, 3);
        break;
    case MOOD_SAD:
    case MOOD_HUNGRY:
    case MOOD_SICK:
        lv_obj_set_size(s_eye_l, 10, 8);
        lv_obj_set_size(s_eye_r, 10, 8);
        break;
    case MOOD_DEAD:
        lv_obj_set_size(s_eye_l, 12, 12);
        lv_obj_set_size(s_eye_r, 12, 12);
        /* X-eyes (rotated squares would be ideal; this is good enough) */
        break;
    default:
        lv_obj_set_size(s_eye_l, 10, 14);
        lv_obj_set_size(s_eye_r, 10, 14);
        break;
    }

    /* Mouth shape by mood */
    switch (m) {
    case MOOD_HAPPY:
    case MOOD_PLAYING:
        lv_obj_set_size(s_mouth, 24, 6);
        lv_obj_align(s_mouth, LV_ALIGN_CENTER, 0, 14);
        break;
    case MOOD_SAD:
    case MOOD_HUNGRY:
        lv_obj_set_size(s_mouth, 18, 4);
        lv_obj_align(s_mouth, LV_ALIGN_CENTER, 0, 18);
        break;
    case MOOD_SICK:
        lv_obj_set_size(s_mouth, 14, 4);
        lv_obj_align(s_mouth, LV_ALIGN_CENTER, 0, 16);
        break;
    case MOOD_SLEEPING:
        lv_obj_set_size(s_mouth, 12, 3);
        lv_obj_align(s_mouth, LV_ALIGN_CENTER, 0, 14);
        break;
    default:
        lv_obj_set_size(s_mouth, 20, 4);
        lv_obj_align(s_mouth, LV_ALIGN_CENTER, 0, 14);
        break;
    }
}

void pet_renderer_set_state(const pet_state_t *p)
{
    pet_mood_t mood = pet_renderer_derive_mood(p);
    pet_stage_t stage = p->stage;

    if (stage != s_stage) {
        int sz = stage_body_size(stage);
        lv_obj_set_size(s_body, sz, sz);
    }
    s_stage = stage;
    s_mood  = mood;

    lv_obj_set_style_bg_color(s_body, stage_body_color(stage, mood), 0);

    /* Egg: hide face */
    bool show_face = (stage != STAGE_EGG);
    if (show_face) {
        lv_obj_clear_flag(s_eye_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_eye_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_mouth, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_eye_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_eye_r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_mouth, LV_OBJ_FLAG_HIDDEN);
    }

    apply_mood_to_face(mood);

    /* Sleep + death indicators */
    if (mood == MOOD_SLEEPING) lv_obj_clear_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);
    else                       lv_obj_add_flag(s_zzz, LV_OBJ_FLAG_HIDDEN);

    if (mood == MOOD_DEAD)     lv_obj_clear_flag(s_halo, LV_OBJ_FLAG_HIDDEN);
    else                       lv_obj_add_flag(s_halo, LV_OBJ_FLAG_HIDDEN);

    /* Poops */
    for (int i = 0; i < 3; i++) {
        if (i < p->poop_count) lv_obj_clear_flag(s_poops[i], LV_OBJ_FLAG_HIDDEN);
        else                   lv_obj_add_flag(s_poops[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void pet_renderer_tick(void)
{
    s_frame++;

    /* Idle bob: ±4px sine, 30 Hz timer → ~2s period at /60 */
    float t = s_frame * (2.0f * (float)M_PI / 60.0f);
    int bob_y = (int)(4.0f * sinf(t));

    /* Sleep mood: slower deeper bob */
    if (s_mood == MOOD_SLEEPING) {
        bob_y = (int)(2.0f * sinf(t * 0.4f));
    }
    /* Dead: no animation */
    if (s_mood == MOOD_DEAD) bob_y = 0;

    lv_obj_align(s_body,  LV_ALIGN_CENTER, 0, bob_y);
    lv_obj_align(s_eye_l, LV_ALIGN_CENTER, -18, -10 + bob_y);
    lv_obj_align(s_eye_r, LV_ALIGN_CENTER,  18, -10 + bob_y);

    /* Blink every 3s for awake moods */
    bool blink = (s_frame % 90 == 0) &&
                 s_mood != MOOD_SLEEPING && s_mood != MOOD_DEAD &&
                 s_stage != STAGE_EGG;
    if (blink) {
        lv_obj_set_size(s_eye_l, 10, 2);
        lv_obj_set_size(s_eye_r, 10, 2);
    } else if ((s_frame - 1) % 90 == 0) {
        apply_mood_to_face(s_mood);  /* restore eye shape */
    }
}
