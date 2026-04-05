#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PARTICLES  40

typedef struct {
    float x, y;
    float vx, vy;
    float life;       /* 1.0 = alive, 0.0 = dead */
    float decay;      /* life reduction per frame */
    lv_color_t color;
    uint8_t radius;
} particle_t;

/** Create the particle container on the given parent */
void particle_engine_init(lv_obj_t *parent);

/** Spawn count particles at (cx, cy) with random velocities */
void particle_spawn(float cx, float cy, int count,
                    lv_color_t color, float speed, float life);

/** Update all particle positions with gravity from tilt */
void particle_update(float grav_x, float grav_y, float dt);

/** Kill all particles */
void particle_clear(void);

/** Get number of alive particles */
int particle_alive_count(void);

#ifdef __cplusplus
}
#endif
