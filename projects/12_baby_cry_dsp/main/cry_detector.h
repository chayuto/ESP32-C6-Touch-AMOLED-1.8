#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRY_SPECTRUM_BINS  32  /**< Number of spectrum bins exposed to UI */

/** Detection state */
typedef enum {
    CRY_STATE_IDLE = 0,      /**< Listening, no cry detected */
    CRY_STATE_DETECTED,      /**< Baby crying detected */
} cry_state_t;

/** Detection statistics + visualization data */
typedef struct {
    cry_state_t state;
    uint32_t    cry_count;
    int64_t     last_cry_time;   /**< us since boot */
    float       cry_band_ratio;
    float       rms_energy;
    float       noise_floor;     /**< Adaptive noise floor */
    float       threshold;       /**< Current adaptive threshold */
    uint8_t     spectrum[CRY_SPECTRUM_BINS]; /**< 0-255 normalized spectrum for UI */
    uint8_t     cry_bin_low;     /**< First bin in cry band (for UI highlight) */
    uint8_t     cry_bin_high;    /**< Last bin in cry band */
    int         periodicity;     /**< Last periodicity count */
} cry_detector_status_t;

esp_err_t cry_detector_init(void);
void cry_detector_start_task(void);
void cry_detector_get_status(cry_detector_status_t *out);

#ifdef __cplusplus
}
#endif
