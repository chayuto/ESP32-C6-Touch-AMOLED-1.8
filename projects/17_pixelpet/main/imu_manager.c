/*
 * imu_manager.c — QMI8658 6-axis IMU driver wrapper
 *
 * Reads accelerometer + gyroscope at ~30 Hz, computes tilt and shake features.
 * Shared state is read by LVGL timer callbacks (no mutex needed — atomic-ish floats).
 */

#include "imu_manager.h"
#include "amoled.h"
#include "qmi8658.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "imu";

static qmi8658_dev_t s_dev;
static volatile imu_state_t s_state;

/* Gravity estimate via low-pass filter */
static float s_grav_x = 0.0f, s_grav_y = 0.0f, s_grav_z = 9.81f;

/* EMA smoothing for motion energy */
static float s_energy_avg = 0.0f;

#define SHAKE_THRESHOLD  3.0f   /* m/s² dynamic acceleration to count as shake */
#define GRAVITY_LPF      0.9f   /* Low-pass alpha for gravity estimate */
#define ENERGY_LPF       0.85f  /* Low-pass for motion energy */

esp_err_t imu_manager_init(void)
{
    i2c_master_bus_handle_t bus = amoled_get_i2c_bus();
    if (!bus) {
        ESP_LOGE(TAG, "I2C bus not available");
        return ESP_FAIL;
    }

    esp_err_t ret = qmi8658_init(&s_dev, bus, QMI8658_ADDRESS_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    qmi8658_set_accel_range(&s_dev, QMI8658_ACCEL_RANGE_4G);
    qmi8658_set_accel_odr(&s_dev, QMI8658_ACCEL_ODR_125HZ);
    qmi8658_set_gyro_range(&s_dev, QMI8658_GYRO_RANGE_512DPS);
    qmi8658_set_gyro_odr(&s_dev, QMI8658_GYRO_ODR_125HZ);
    qmi8658_set_accel_unit_mps2(&s_dev, true);
    qmi8658_set_gyro_unit_rads(&s_dev, true);

    ESP_LOGI(TAG, "QMI8658 initialized: ±4g, 125Hz, ±512dps");
    return ESP_OK;
}

static void imu_task(void *arg)
{
    ESP_LOGI(TAG, "IMU task started");
    qmi8658_data_t data;

    while (1) {
        bool ready = false;
        esp_err_t ret = qmi8658_is_data_ready(&s_dev, &ready);
        if (ret != ESP_OK || !ready) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        ret = qmi8658_read_sensor_data(&s_dev, &data);
        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        float ax = data.accelX;
        float ay = data.accelY;
        float az = data.accelZ;

        /* Update gravity estimate (low-pass filter) */
        s_grav_x = GRAVITY_LPF * s_grav_x + (1.0f - GRAVITY_LPF) * ax;
        s_grav_y = GRAVITY_LPF * s_grav_y + (1.0f - GRAVITY_LPF) * ay;
        s_grav_z = GRAVITY_LPF * s_grav_z + (1.0f - GRAVITY_LPF) * az;

        /* Dynamic acceleration (high-pass: remove gravity) */
        float dx = ax - s_grav_x;
        float dy = ay - s_grav_y;
        float dz = az - s_grav_z;
        float shake_mag = sqrtf(dx * dx + dy * dy + dz * dz);

        /* Motion energy (smoothed) */
        s_energy_avg = ENERGY_LPF * s_energy_avg + (1.0f - ENERGY_LPF) * shake_mag;

        /* Tilt: normalize gravity vector → screen coordinates.
         * The QMI8658 is mounted rotated 90° CCW on the back of the PCB
         * relative to the portrait display. Mapping (verified on hardware):
         *   screen_x = -chip_y   (tilt right → positive screen X)
         *   screen_y =  chip_x   (tilt forward/top drops → positive screen Y) */
        float grav_len = sqrtf(s_grav_x * s_grav_x + s_grav_y * s_grav_y + s_grav_z * s_grav_z);
        float tilt_x = 0.0f, tilt_y = 0.0f;
        if (grav_len > 1.0f) {
            tilt_x = -s_grav_y / grav_len;
            tilt_y = s_grav_x / grav_len;
            /* Clamp */
            if (tilt_x > 1.0f) tilt_x = 1.0f;
            if (tilt_x < -1.0f) tilt_x = -1.0f;
            if (tilt_y > 1.0f) tilt_y = 1.0f;
            if (tilt_y < -1.0f) tilt_y = -1.0f;
        }

        /* Write shared state */
        imu_state_t st;
        st.accel_x = ax;
        st.accel_y = ay;
        st.accel_z = az;
        st.tilt_x = tilt_x;
        st.tilt_y = tilt_y;
        st.shake_mag = shake_mag;
        st.motion_energy = s_energy_avg;
        st.is_shaking = (shake_mag > SHAKE_THRESHOLD);
        st.last_update_us = esp_timer_get_time();
        s_state = st;

        vTaskDelay(pdMS_TO_TICKS(33));  /* ~30 Hz */
    }
}

void imu_manager_start_task(void)
{
    xTaskCreate(imu_task, "imu", 4096, NULL, 3, NULL);
}

void imu_manager_get_state(imu_state_t *out)
{
    *out = s_state;
}
