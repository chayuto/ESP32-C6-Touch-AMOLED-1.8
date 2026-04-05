#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float accel_x, accel_y, accel_z;  /* m/s², raw including gravity */
    float tilt_x, tilt_y;            /* normalized -1.0 to 1.0 from gravity */
    float shake_mag;                  /* high-pass filtered magnitude (m/s²) */
    float motion_energy;              /* rolling average of dynamic accel */
    bool  is_shaking;                 /* shake_mag > threshold */
    int64_t last_update_us;
} imu_state_t;

esp_err_t imu_manager_init(void);
void imu_manager_start_task(void);
void imu_manager_get_state(imu_state_t *out);

#ifdef __cplusplus
}
#endif
