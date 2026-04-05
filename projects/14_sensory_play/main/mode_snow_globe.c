/*
 * mode_snow_globe.c — Calm particle mode
 *
 * Shake to scatter glittering particles.
 * Tilt to drift them with gravity.
 * They settle slowly and fade.
 */

#include "mode_snow_globe.h"
#include "particle_engine.h"
#include "audio_output.h"
#include "amoled.h"
#include "esp_timer.h"

static lv_obj_t *s_container = NULL;
static bool s_was_shaking = false;
static int64_t s_last_chime_us = 0;

/* Snow colors */
static lv_color_t snow_color(void)
{
    int r = rand() % 3;
    if (r == 0) return lv_color_make(200, 220, 255);  /* light blue */
    if (r == 1) return lv_color_make(255, 255, 255);  /* white */
    return lv_color_make(180, 230, 255);               /* cyan-ish */
}

void mode_snow_globe_init(lv_obj_t *parent)
{
    s_container = parent;
    lv_obj_set_style_bg_color(parent, lv_color_make(10, 10, 46), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    particle_engine_init(parent);
}

void mode_snow_globe_enter(void)
{
    particle_clear();
    s_was_shaking = false;
    s_last_chime_us = 0;

    /* Spawn some initial particles */
    for (int i = 0; i < 8; i++) {
        float x = 50 + (rand() % (AMOLED_LCD_H_RES - 100));
        float y = 50 + (rand() % (AMOLED_LCD_V_RES - 100));
        particle_spawn(x, y, 1, snow_color(), 1.0f, 1.0f);
    }
}

void mode_snow_globe_update(const imu_state_t *imu)
{
    int64_t now = esp_timer_get_time();

    /* Shake → burst of particles from center */
    if (imu->is_shaking && !s_was_shaking) {
        float cx = AMOLED_LCD_H_RES / 2.0f;
        float cy = AMOLED_LCD_V_RES / 2.0f;
        int count = 8 + (rand() % 8);
        float speed = 2.0f + imu->shake_mag * 0.5f;
        particle_spawn(cx, cy, count, snow_color(), speed, 1.0f);

        /* Chime sound (rate-limited) */
        if (now - s_last_chime_us > 300000) {  /* 300ms cooldown */
            float freq = 800.0f + (float)(rand() % 400);
            audio_output_play_tone(freq, 150, 0.25f);
            s_last_chime_us = now;
        }
    }
    s_was_shaking = imu->is_shaking;

    /* Gentle ambient spawn when few particles alive */
    if (particle_alive_count() < 5 && (rand() % 10) == 0) {
        float x = (float)(rand() % AMOLED_LCD_H_RES);
        particle_spawn(x, 20.0f, 1, snow_color(), 0.5f, 1.0f);
    }

    /* Update particles with gravity from tilt */
    particle_update(imu->tilt_x, imu->tilt_y, 1.0f / 30.0f);
}

void mode_snow_globe_exit(void)
{
    particle_clear();
}
