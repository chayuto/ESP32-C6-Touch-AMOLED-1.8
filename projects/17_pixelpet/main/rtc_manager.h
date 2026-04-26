#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise PCF85063 over I2C; reads current time, falls back to a sane
 *  seed (2026-01-01 00:00:00 UTC) on first boot when the RTC reports the
 *  oscillator-stopped flag. Must be called after amoled_init(). */
esp_err_t rtc_manager_init(void);

/** Was a real, running RTC value read at init? false → first-boot seed. */
bool rtc_manager_is_valid(void);

/** Current unix-epoch seconds, computed from boot snapshot + esp_timer. */
int64_t rtc_manager_now_unix(void);

/** Force a re-read of the PCF85063 to correct drift. */
esp_err_t rtc_manager_resync(void);

/** Set the wall-clock time (used to seed the RTC on first boot or to set
 *  the time from network later). */
esp_err_t rtc_manager_set_unix(int64_t unix_s);

#ifdef __cplusplus
}
#endif
