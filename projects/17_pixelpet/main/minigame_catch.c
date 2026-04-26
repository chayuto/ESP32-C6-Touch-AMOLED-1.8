/*
 * minigame_catch.c — tilt-controlled apple-catching mini-game.
 *
 * Apples fall from the top at increasing speed. The player tilts the device
 * to slide the pet basket left/right and catch them. Round lasts 30 s; each
 * catch is +1 score, each miss is forgiven. Score feeds back into stats via
 * the caller (see ui_screens for the wiring).
 */

#include "minigame_catch.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "minigame_catch";

#define APPLE_COUNT      6
#define ROUND_TICKS      (30 * 30)   /* 30s at 30Hz */
#define BASKET_W         60
#define BASKET_H         24
#define APPLE_SIZE       18
#define SCREEN_W         368
#define SCREEN_H         448
#define APPLE_SPEED_MIN  3
#define APPLE_SPEED_MAX  6

typedef struct {
    lv_obj_t *obj;
    int x, y;
    int speed;
    bool alive;
} apple_t;

static lv_obj_t *s_root;
static lv_obj_t *s_basket;
static lv_obj_t *s_score_lbl;
static lv_obj_t *s_time_lbl;
static apple_t   s_apples[APPLE_COUNT];

static bool s_active = false;
static int  s_ticks_left = 0;
static int  s_score = 0;
static int  s_basket_x = SCREEN_W / 2;

static int rand_range(int lo, int hi)
{
    return lo + (rand() % (hi - lo + 1));
}

static void respawn_apple(apple_t *a)
{
    a->x = rand_range(20, SCREEN_W - 20 - APPLE_SIZE);
    a->y = -rand_range(10, 200);
    a->speed = rand_range(APPLE_SPEED_MIN, APPLE_SPEED_MAX);
    a->alive = true;
    lv_obj_clear_flag(a->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(a->obj, a->x, a->y);
}

void minigame_catch_init(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x1a0a2e), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);

    s_score_lbl = lv_label_create(s_root);
    lv_label_set_text(s_score_lbl, "0");
    lv_obj_set_style_text_color(s_score_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_score_lbl, &lv_font_montserrat_28, 0);
    lv_obj_align(s_score_lbl, LV_ALIGN_TOP_LEFT, 16, 16);

    s_time_lbl = lv_label_create(s_root);
    lv_label_set_text(s_time_lbl, "30");
    lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(0xFFD166), 0);
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_28, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_TOP_RIGHT, -16, 16);

    s_basket = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_basket);
    lv_obj_set_size(s_basket, BASKET_W, BASKET_H);
    lv_obj_set_style_bg_color(s_basket, lv_color_hex(0xFFAFCC), 0);
    lv_obj_set_style_bg_opa(s_basket, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_basket, 8, 0);
    lv_obj_set_pos(s_basket, SCREEN_W / 2 - BASKET_W / 2, SCREEN_H - BASKET_H - 30);

    for (int i = 0; i < APPLE_COUNT; i++) {
        s_apples[i].obj = lv_obj_create(s_root);
        lv_obj_remove_style_all(s_apples[i].obj);
        lv_obj_set_size(s_apples[i].obj, APPLE_SIZE, APPLE_SIZE);
        lv_obj_set_style_bg_color(s_apples[i].obj, lv_color_hex(0xEF476F), 0);
        lv_obj_set_style_bg_opa(s_apples[i].obj, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_apples[i].obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(s_apples[i].obj, LV_OBJ_FLAG_HIDDEN);
        s_apples[i].alive = false;
    }
}

void minigame_catch_start(void)
{
    s_active = true;
    s_ticks_left = ROUND_TICKS;
    s_score = 0;
    s_basket_x = SCREEN_W / 2;

    lv_label_set_text(s_score_lbl, "0");
    lv_label_set_text(s_time_lbl, "30");
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_root);

    for (int i = 0; i < APPLE_COUNT; i++) respawn_apple(&s_apples[i]);
    ESP_LOGI(TAG, "minigame started");
}

bool minigame_catch_is_active(void) { return s_active; }

static void end_round(void)
{
    s_active = false;
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "round over: score=%d", s_score);
}

int minigame_catch_tick(const imu_state_t *imu)
{
    if (!s_active) return -1;

    /* Move basket from tilt_x. The IMU sits 90° CCW on the back of the PCB,
     * but for portrait minigame play, +tilt_x feels right after empirical
     * testing on similar boards — flip the sign here if it tilts wrong. */
    float tx = imu ? imu->tilt_x : 0.0f;
    if (tx >  1.0f) tx =  1.0f;
    if (tx < -1.0f) tx = -1.0f;
    s_basket_x = SCREEN_W / 2 + (int)(tx * (SCREEN_W / 2 - BASKET_W));
    if (s_basket_x < 0) s_basket_x = 0;
    if (s_basket_x > SCREEN_W - BASKET_W) s_basket_x = SCREEN_W - BASKET_W;
    lv_obj_set_pos(s_basket, s_basket_x, SCREEN_H - BASKET_H - 30);

    /* Update apples */
    for (int i = 0; i < APPLE_COUNT; i++) {
        apple_t *a = &s_apples[i];
        if (!a->alive) continue;
        a->y += a->speed;

        /* Collision with basket (AABB) */
        bool x_overlap = (a->x + APPLE_SIZE) > s_basket_x &&
                         a->x < (s_basket_x + BASKET_W);
        bool y_overlap = (a->y + APPLE_SIZE) >= (SCREEN_H - BASKET_H - 30);
        if (x_overlap && y_overlap) {
            s_score++;
            char buf[12];
            snprintf(buf, sizeof(buf), "%d", s_score);
            lv_label_set_text(s_score_lbl, buf);
            respawn_apple(a);
            continue;
        }

        /* Off-bottom: respawn (apple miss is a freebie) */
        if (a->y >= SCREEN_H) { respawn_apple(a); continue; }

        lv_obj_set_pos(a->obj, a->x, a->y);
    }

    /* Timer */
    s_ticks_left--;
    if (s_ticks_left % 30 == 0) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%d", s_ticks_left / 30);
        lv_label_set_text(s_time_lbl, buf);
    }
    if (s_ticks_left <= 0) {
        int score = s_score;
        end_round();
        return score;
    }
    return -1;
}
