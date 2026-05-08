#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float    temp_c;
    float    humid_pct;
    uint8_t  battery_pct;
} govee_reading_t;

/* Decode an H5075 manufacturer-data payload.
 *
 * Input is the *payload after* the 2-byte company-ID prefix
 * (i.e. 6 bytes for a clean H5075 advert). Returns true if the decode
 * passed sanity gates (-40..100°C, 0..100%RH, no error flag). */
bool govee_decode_h5075(const uint8_t *payload, uint8_t len, govee_reading_t *out);
