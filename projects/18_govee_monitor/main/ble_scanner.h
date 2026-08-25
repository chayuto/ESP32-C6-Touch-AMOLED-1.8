#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Initialize NimBLE host + controller, start passive scan with no duplicate
 * filtering. Phase 2: every advert with a manufacturer-data block is logged
 * raw (company ID + bytes). Phases 3+ replace the raw log with a Govee
 * decoder + slot store update. */
esp_err_t ble_scanner_start(void);

/* Pause: stop the active scan but keep the NimBLE host alive so resume can
 * restart in milliseconds. Idempotent. */
esp_err_t ble_scanner_pause(void);

/* Resume: re-enter passive scan with the same parameters as start(). */
esp_err_t ble_scanner_resume(void);

/* True while a scan is active. */
bool      ble_scanner_is_running(void);

/* Decoded H5075 adverts since boot — for the health heartbeat. */
uint32_t ble_scanner_advert_count(void);
