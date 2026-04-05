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
    /* v3 scoring debug fields */
    int         score;           /**< Multi-feature score (0-100) */
    float       low_ratio;       /**< Energy < 250 Hz / total */
    float       high_ratio;      /**< Energy 1-3.5 kHz / total */
    float       crest;           /**< Spectral crest factor */
    int         harmonic_pct;    /**< % of frames with verified harmonics */
    int         f0_hz;           /**< Detected F0 frequency in Hz */
    bool        cry_dominant;    /**< Cry band energy > low band energy */
    bool        gated;           /**< True if hard gates blocked scoring */
    int         pos_streak;      /**< Consecutive positive detections */
    int         neg_streak;      /**< Consecutive negative detections */
    /* v3.1 Torres HCBC features */
    float       f0_variance;     /**< F0 bin variance across frames (low = stable pitch) */
    float       voiced_ratio;    /**< Fraction of frames with detectable F0 (0.0-1.0) */
    int         max_consec_f0;   /**< Longest consecutive F0 run (frames) */
} cry_detector_status_t;

esp_err_t cry_detector_init(void);
void cry_detector_start_task(void);
void cry_detector_get_status(cry_detector_status_t *out);

#ifdef __cplusplus
}
#endif
