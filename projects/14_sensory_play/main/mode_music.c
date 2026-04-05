/*
 * mode_music.c — Music Maker mode
 *
 * Four drum pad regions on screen. Tap to play sounds.
 * Shake for maracas. Tilt bends pitch (future).
 */

#include "mode_music.h"
#include "particle_engine.h"
#include "audio_output.h"
#include "amoled.h"
#include "esp_timer.h"

#include <stdlib.h>

static lv_obj_t *s_container = NULL;
static lv_obj_t *s_pad_labels[4] = {NULL};
static lv_obj_t *s_pads[4] = {NULL};
static bool s_was_shaking = false;

/* Pad layout: 2×2 grid */
#define PAD_W  (AMOLED_LCD_H_RES / 2)
#define PAD_H  ((AMOLED_LCD_V_RES - 60) / 2)  /* leave room for title */
#define PAD_Y_OFFSET 60

/* Pad definitions */
static const struct {
    const char *label;
    lv_color_t color;
    float freq;         /* 0 = noise-based */
    int duration_ms;
} s_pad_defs[4] = {
    { "KICK",   {.full = 0xF800}, 100.0f, 150 },   /* red, low tone */
    { "SNARE",  {.full = 0xFFE0}, 0.0f,   80  },   /* yellow, noise */
    { "HI-HAT", {.full = 0x07E0}, 0.0f,   40  },   /* green, short noise */
    { "BELL",   {.full = 0x001F}, 800.0f,  200 },   /* blue, high tone */
};

void mode_music_init(lv_obj_t *parent)
{
    s_container = parent;
    lv_obj_set_style_bg_color(parent, lv_color_make(26, 10, 46), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Music Maker");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* Shake hint */
    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "Shake for maracas!");
    lv_obj_set_style_text_color(hint, lv_color_make(150, 150, 150), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 36);

    /* Create 4 pads */
    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;

        s_pads[i] = lv_obj_create(parent);
        lv_obj_set_size(s_pads[i], PAD_W - 8, PAD_H - 8);
        lv_obj_set_pos(s_pads[i], col * PAD_W + 4, PAD_Y_OFFSET + row * PAD_H + 4);
        lv_obj_set_style_bg_color(s_pads[i], s_pad_defs[i].color, 0);
        lv_obj_set_style_bg_opa(s_pads[i], LV_OPA_70, 0);
        lv_obj_set_style_border_color(s_pads[i], lv_color_white(), 0);
        lv_obj_set_style_border_width(s_pads[i], 2, 0);
        lv_obj_set_style_radius(s_pads[i], 12, 0);
        lv_obj_clear_flag(s_pads[i], LV_OBJ_FLAG_SCROLLABLE);

        s_pad_labels[i] = lv_label_create(s_pads[i]);
        lv_label_set_text(s_pad_labels[i], s_pad_defs[i].label);
        lv_obj_set_style_text_color(s_pad_labels[i], lv_color_white(), 0);
        lv_obj_set_style_text_font(s_pad_labels[i], &lv_font_montserrat_20, 0);
        lv_obj_center(s_pad_labels[i]);
    }
}

void mode_music_enter(void)
{
    particle_clear();
    s_was_shaking = false;
    /* Reset pad opacity */
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_opa(s_pads[i], LV_OPA_70, 0);
    }
}

static int get_pad_index(int16_t x, int16_t y)
{
    if (y < PAD_Y_OFFSET) return -1;
    int col = (x < PAD_W) ? 0 : 1;
    int row = ((y - PAD_Y_OFFSET) < PAD_H) ? 0 : 1;
    return row * 2 + col;
}

void mode_music_on_touch(int16_t x, int16_t y)
{
    int pad = get_pad_index(x, y);
    if (pad < 0 || pad >= 4) return;

    /* Flash the pad */
    lv_obj_set_style_bg_opa(s_pads[pad], LV_OPA_COVER, 0);

    /* Play sound */
    if (s_pad_defs[pad].freq > 0) {
        audio_output_play_tone(s_pad_defs[pad].freq,
                               s_pad_defs[pad].duration_ms, 0.5f);
    } else {
        audio_output_play_noise(s_pad_defs[pad].duration_ms, 0.4f);
    }

    /* Spawn particles from pad center */
    float cx = (pad % 2) * PAD_W + PAD_W / 2.0f;
    float cy = PAD_Y_OFFSET + (pad / 2) * PAD_H + PAD_H / 2.0f;
    particle_spawn(cx, cy, 5, s_pad_defs[pad].color, 3.0f, 0.6f);
}

void mode_music_on_release(void)
{
    /* Restore pad opacity */
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_opa(s_pads[i], LV_OPA_70, 0);
    }
}

void mode_music_update(const imu_state_t *imu)
{
    /* Shake → maracas */
    if (imu->is_shaking && !s_was_shaking) {
        audio_output_play_noise(60, 0.35f);
        /* Scatter particles from edges */
        particle_spawn(AMOLED_LCD_H_RES / 2.0f, AMOLED_LCD_V_RES / 2.0f,
                       6, lv_color_make(255, 200, 50), 3.0f, 0.5f);
    }
    s_was_shaking = imu->is_shaking;

    /* Update particles with slight gravity from tilt */
    particle_update(imu->tilt_x * 0.3f, imu->tilt_y * 0.3f, 1.0f / 30.0f);
}

void mode_music_exit(void)
{
    particle_clear();
}
