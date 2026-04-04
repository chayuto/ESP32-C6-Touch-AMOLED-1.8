#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start SNTP sync (non-blocking, retries periodically). Call after WiFi connected. */
void ntp_time_start(void);

/** Is time synchronized? */
bool ntp_time_is_synced(void);

/** Get current time as formatted string "HH:MM:SS". Returns false if not synced. */
bool ntp_time_get_str(char *buf, size_t len);

/** Get current time_t. Returns 0 if not synced. */
time_t ntp_time_get(void);

/** Is WiFi currently on? (off most of the time to save power) */
bool ntp_time_is_wifi_on(void);

#ifdef __cplusplus
}
#endif
