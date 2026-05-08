#pragma once

#include <stdint.h>
#include "esp_err.h"

/* Initialize NimBLE host + controller, start passive scan with no duplicate
 * filtering. Phase 2: every advert with a manufacturer-data block is logged
 * raw (company ID + bytes). Phases 3+ replace the raw log with a Govee
 * decoder + slot store update. */
esp_err_t ble_scanner_start(void);
