/*
 * mode_dance.c — Dance Party mode
 *
 * Plays a repeating melody. Motion creates colorful particle bursts.
 * More movement = more visual excitement.
 */

#include "mode_dance.h"
#include "particle_engine.h"
#include "audio_output.h"
#include "amoled.h"
#include "esp_timer.h"

#include <stdlib.h>

static lv_obj_t *s_container = NULL;
static int64_t s_last_beat_us = 0;
static int s_beat_idx = 0;
static int64_t s_last_motion_spawn_us = 0;
static bool s_was_shaking = false;

/* C major pentatonic melody (frequencies in Hz) */
static const float s_melody[] = {
    523.25f, 587.33f, 659.25f, 783.99f, 880.00f,  /* C5 D5 E5 G5 A5 */
    783.99f, 659.25f, 587.33f, 523.25f, 440.00f,  /* G5 E5 D5 C5 A4 */
    523.25f, 659.25f, 783.99f, 1046.5f, 880.00f,  /* C5 E5 G5 C6 A5 */
    783.99f, 659.25f, 523.25f, 440.00f, 523.25f,  /* G5 E5 C5 A4 C5 */
};
#define MELODY_LEN  (sizeof(s_melody) / sizeof(s_melody[0]))
#define BEAT_INTERVAL_US  250000  /* 250ms = 240 BPM (fast and fun) */

/* Rainbow colors */
static lv_color_t rainbow_color(void)
{
    int r = rand() % 6;
    switch (r) {
        case 0: return lv_color_make(255, 50, 50);    /* red */
        case 1: return lv_color_make(255, 165, 0);    /* orange */
        case 2: return lv_color_make(255, 255, 50);   /* yellow */
        case 3: return lv_color_make(50, 255, 50);    /* green */
        case 4: return lv_color_make(50, 100, 255);   /* blue */
        default: return lv_color_make(200, 50, 255);  /* purple */
    }
}

void mode_dance_init(lv_obj_t *parent)
{
    s_container = parent;
    lv_obj_set_style_bg_color(parent, lv_color_make(5, 5, 15), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
}

void mode_dance_enter(void)
{
    particle_clear();
    s_beat_idx = 0;
    s_last_beat_us = esp_timer_get_time();
    s_last_motion_spawn_us = 0;
    s_was_shaking = false;
}

void mode_dance_update(const imu_state_t *imu)
{
    int64_t now = esp_timer_get_time();

    /* Auto-play melody on beat */
    if (now - s_last_beat_us >= BEAT_INTERVAL_US) {
        s_last_beat_us = now;
        float freq = s_melody[s_beat_idx % MELODY_LEN];
        audio_output_play_tone(freq, 120, 0.3f);
        s_beat_idx++;

        /* Beat visual: spawn a few particles at random position */
        float x = 40.0f + (float)(rand() % (AMOLED_LCD_H_RES - 80));
        float y = 40.0f + (float)(rand() % (AMOLED_LCD_V_RES - 80));
        particle_spawn(x, y, 2, rainbow_color(), 2.0f, 0.8f);
    }

    /* Motion-reactive: more movement → more particles */
    if (imu->motion_energy > 0.5f && now - s_last_motion_spawn_us > 100000) {
        s_last_motion_spawn_us = now;
        int count = (int)(imu->motion_energy * 2.0f);
        if (count > 6) count = 6;
        if (count < 1) count = 1;
        float cx = AMOLED_LCD_H_RES / 2.0f + imu->tilt_x * 100.0f;
        float cy = AMOLED_LCD_V_RES / 2.0f + imu->tilt_y * 100.0f;
        particle_spawn(cx, cy, count, rainbow_color(),
                       1.0f + imu->motion_energy, 0.7f);
    }

    /* Big shake → fireworks burst + cymbal */
    if (imu->is_shaking && !s_was_shaking) {
        float cx = AMOLED_LCD_H_RES / 2.0f;
        float cy = AMOLED_LCD_V_RES / 2.0f;
        for (int i = 0; i < 3; i++) {
            particle_spawn(cx, cy, 5, rainbow_color(), 4.0f, 1.0f);
        }
        audio_output_play_noise(80, 0.4f);
    }
    s_was_shaking = imu->is_shaking;

    /* Update particles — no gravity in dance mode (float freely) */
    particle_update(0.0f, 0.0f, 1.0f / 30.0f);
}

void mode_dance_exit(void)
{
    particle_clear();
}
