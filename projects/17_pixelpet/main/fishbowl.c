/*
 * fishbowl.c — drifting-bubble backdrop + day/night tint.
 *
 * 8 bubble lv_obj's drawn behind the pet, each rising with a sinusoidal
 * x-wobble at a varied speed and respawning at the bottom when they
 * leave the visible area. This is the "chilling" / fishbowl feel —
 * nothing demanding, just constant gentle motion.
 *
 * The tint layer is a full-screen translucent rectangle whose colour
 * shifts based on RTC hour: warm gold during the day, cool blue at
 * night. Applied via lv_obj's bg_color + bg_opa style.
 */

#include "fishbowl.h"
#include "asset_loader.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>

static const char *TAG = "fishbowl";

#define BUBBLE_COUNT      8
#define SCREEN_W          368
#define SCREEN_H          448

typedef struct {
    lv_obj_t *obj;
    int       x_base;        /* x-axis centreline (pixels) */
    int       y;             /* current y (pixels) */
    int       speed;         /* y per tick */
    int       phase;         /* sine phase counter */
    int       wobble_amp;    /* how much sideways drift */
    int       size;          /* render size in px */
} bubble_t;

static lv_obj_t *s_root;
static lv_obj_t *s_tint;
static bubble_t  s_bubbles[BUBBLE_COUNT];

static int rand_range(int lo, int hi)
{
    return lo + (rand() % (hi - lo + 1));
}

static void respawn_bubble(bubble_t *b)
{
    b->x_base    = rand_range(20, SCREEN_W - 20);
    b->y         = SCREEN_H + rand_range(0, 80);   /* below screen */
    b->speed     = rand_range(1, 3);
    b->phase     = rand_range(0, 360);
    b->wobble_amp = rand_range(2, 8);
    b->size      = rand_range(10, 22);
    lv_obj_set_size(b->obj, b->size, b->size);
}

void fishbowl_init(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Tint layer covers the whole screen, sits below the pet but above
     * any solid background. Initial neutral tint. */
    s_tint = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_tint);
    lv_obj_set_size(s_tint, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_tint, lv_color_hex(0x0a1c2e), 0);
    lv_obj_set_style_bg_opa(s_tint, LV_OPA_70, 0);
    lv_obj_clear_flag(s_tint, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Try to use the bubble sprite from the bundle; fall back to a
     * procedural circle obj if the asset isn't available yet. */
    const asset_anim_t *bubble_asset = asset_get_by_name("particles/bubble");

    for (int i = 0; i < BUBBLE_COUNT; i++) {
        if (bubble_asset) {
            s_bubbles[i].obj = lv_img_create(s_root);
            lv_img_set_src(s_bubbles[i].obj, bubble_asset->frames[0]);
            lv_img_set_antialias(s_bubbles[i].obj, false);
            lv_img_set_zoom(s_bubbles[i].obj, 256 * 2);  /* 8 → 16 px */
        } else {
            s_bubbles[i].obj = lv_obj_create(s_root);
            lv_obj_remove_style_all(s_bubbles[i].obj);
            lv_obj_set_style_radius(s_bubbles[i].obj, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(s_bubbles[i].obj, LV_OPA_50, 0);
            lv_obj_set_style_bg_color(s_bubbles[i].obj,
                                      lv_color_hex(0xC0E0F8), 0);
        }
        lv_obj_clear_flag(s_bubbles[i].obj,
                          LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        respawn_bubble(&s_bubbles[i]);
        /* Stagger initial heights so they're already mid-screen at boot. */
        s_bubbles[i].y = rand_range(0, SCREEN_H);
    }

    lv_obj_move_background(s_root);
    ESP_LOGI(TAG, "fishbowl ready (%d bubbles, sprite=%s)",
             BUBBLE_COUNT, bubble_asset ? "yes" : "fallback");
}

void fishbowl_tick(void)
{
    for (int i = 0; i < BUBBLE_COUNT; i++) {
        bubble_t *b = &s_bubbles[i];
        b->y     -= b->speed;
        b->phase += 7;
        int dx = (int)(b->wobble_amp * sinf(b->phase * (float)M_PI / 180.0f));
        lv_obj_set_pos(b->obj, b->x_base + dx, b->y);
        if (b->y < -32) respawn_bubble(b);
    }
}

void fishbowl_apply_tint(int hour)
{
    /* Warm midday gold (#3A2A0E) at hour 12, cool deep-blue (#0a1c2e) at
     * midnight. Smoothly interpolate by mapping hour to a 0..1 weight on
     * a cosine curve (peaks at noon, troughs at midnight). */
    float t = (float)hour;
    if (t < 0) t = 0;
    if (t > 23) t = 23;
    float day = 0.5f * (1.0f - cosf((t / 24.0f) * 2.0f * (float)M_PI));

    /* Day colour: warm amber overlay (rgb 0x32, 0x22, 0x10).
     * Night colour: cool blue (rgb 0x0a, 0x1c, 0x2e). */
    uint8_t r = (uint8_t)(0x0a + (0x32 - 0x0a) * day);
    uint8_t g = (uint8_t)(0x1c + (0x22 - 0x1c) * day);
    uint8_t b = (uint8_t)(0x2e + (0x10 - 0x2e) * day);
    lv_obj_set_style_bg_color(s_tint, lv_color_make(r, g, b), 0);

    /* Less opacity around noon, more at night (cosier). */
    uint8_t opa = (uint8_t)(70 - 40 * day);
    lv_obj_set_style_bg_opa(s_tint, opa, 0);
}
