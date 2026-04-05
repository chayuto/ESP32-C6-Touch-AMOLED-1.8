/*
 * particle_engine.c — Simple particle system using LVGL objects
 *
 * Each particle is an lv_obj with a rounded background style.
 * Gravity is applied from the IMU tilt vector.
 */

#include "particle_engine.h"
#include "amoled.h"
#include "esp_log.h"
#include <stdlib.h>
#include <math.h>

static const char *TAG = "particle";

static particle_t s_particles[MAX_PARTICLES];
static lv_obj_t  *s_objs[MAX_PARTICLES];
static lv_obj_t  *s_container = NULL;
static lv_style_t s_styles[MAX_PARTICLES];

#define SCREEN_W  AMOLED_LCD_H_RES
#define SCREEN_H  AMOLED_LCD_V_RES
#define GRAVITY_SCALE  120.0f   /* pixels/s² per unit tilt */
#define DAMPING   0.97f
#define BOUNCE    0.5f

/* Simple random float 0.0-1.0 */
static float randf(void)
{
    return (float)(rand() % 1000) / 1000.0f;
}

void particle_engine_init(lv_obj_t *parent)
{
    s_container = parent;

    for (int i = 0; i < MAX_PARTICLES; i++) {
        s_particles[i].life = 0.0f;

        lv_style_init(&s_styles[i]);
        lv_style_set_radius(&s_styles[i], LV_RADIUS_CIRCLE);
        lv_style_set_bg_opa(&s_styles[i], LV_OPA_COVER);
        lv_style_set_bg_color(&s_styles[i], lv_color_white());
        lv_style_set_border_width(&s_styles[i], 0);

        s_objs[i] = lv_obj_create(parent);
        lv_obj_remove_style_all(s_objs[i]);
        lv_obj_add_style(s_objs[i], &s_styles[i], 0);
        lv_obj_set_size(s_objs[i], 8, 8);
        lv_obj_add_flag(s_objs[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_objs[i], LV_OBJ_FLAG_CLICKABLE);
    }

    ESP_LOGI(TAG, "Particle engine initialized (%d max)", MAX_PARTICLES);
}

void particle_spawn(float cx, float cy, int count,
                    lv_color_t color, float speed, float life)
{
    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (s_particles[i].life > 0.0f) continue;

        particle_t *p = &s_particles[i];
        p->x = cx;
        p->y = cy;

        /* Random direction */
        float angle = randf() * 2.0f * 3.14159f;
        float spd = speed * (0.3f + 0.7f * randf());
        p->vx = spd * cosf(angle);
        p->vy = spd * sinf(angle);

        p->life = life;
        p->decay = life / (60.0f * (1.0f + randf()));  /* die in ~1-2 seconds at 30fps */
        p->color = color;
        p->radius = 3 + (rand() % 5);

        /* Update LVGL object */
        lv_style_set_bg_color(&s_styles[i], color);
        lv_obj_set_size(s_objs[i], p->radius * 2, p->radius * 2);
        lv_obj_set_pos(s_objs[i], (int)p->x - p->radius, (int)p->y - p->radius);
        lv_obj_clear_flag(s_objs[i], LV_OBJ_FLAG_HIDDEN);

        spawned++;
    }
}

void particle_update(float grav_x, float grav_y, float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &s_particles[i];
        if (p->life <= 0.0f) continue;

        /* Apply gravity from tilt */
        p->vx += grav_x * GRAVITY_SCALE * dt;
        p->vy += grav_y * GRAVITY_SCALE * dt;

        /* Damping */
        p->vx *= DAMPING;
        p->vy *= DAMPING;

        /* Move */
        p->x += p->vx * dt * 30.0f;  /* scale for ~30fps */
        p->y += p->vy * dt * 30.0f;

        /* Bounce off walls */
        if (p->x < 0) { p->x = 0; p->vx *= -BOUNCE; }
        if (p->x > SCREEN_W - 1) { p->x = SCREEN_W - 1; p->vx *= -BOUNCE; }
        if (p->y < 0) { p->y = 0; p->vy *= -BOUNCE; }
        if (p->y > SCREEN_H - 1) { p->y = SCREEN_H - 1; p->vy *= -BOUNCE; }

        /* Decay */
        p->life -= p->decay;

        if (p->life <= 0.0f) {
            p->life = 0.0f;
            lv_obj_add_flag(s_objs[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            /* Update position and opacity */
            lv_obj_set_pos(s_objs[i], (int)p->x - p->radius, (int)p->y - p->radius);
            lv_opa_t opa = (lv_opa_t)(p->life * 255.0f);
            if (opa < 30) opa = 30;
            lv_style_set_bg_opa(&s_styles[i], opa);
            lv_obj_report_style_change(&s_styles[i]);
        }
    }
}

void particle_clear(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        s_particles[i].life = 0.0f;
        lv_obj_add_flag(s_objs[i], LV_OBJ_FLAG_HIDDEN);
    }
}

int particle_alive_count(void)
{
    int count = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (s_particles[i].life > 0.0f) count++;
    }
    return count;
}
