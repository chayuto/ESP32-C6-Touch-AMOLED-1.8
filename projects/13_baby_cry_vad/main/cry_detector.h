#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Detection state */
typedef enum {
    CRY_STATE_IDLE = 0,      /**< Listening, no cry detected */
    CRY_STATE_VOICE,         /**< VAD detected voice, analyzing */
    CRY_STATE_DETECTED,      /**< Baby crying detected */
} cry_state_t;

/** Detection statistics */
typedef struct {
    cry_state_t state;        /**< Current detection state */
    uint32_t    cry_count;    /**< Total cry events detected */
    int64_t     last_cry_time; /**< Timestamp of last detection (us since boot) */
    float       cry_band_ratio; /**< Last computed cry band energy ratio */
    float       rms_energy;   /**< Last computed RMS energy */
    bool        vad_active;   /**< VAD currently detecting voice */
} cry_detector_status_t;

/**
 * Initialize the cry detector: allocate FFT workspace, buffers.
 */
esp_err_t cry_detector_init(void);

/**
 * Start the cry detection task.
 */
void cry_detector_start_task(void);

/**
 * Get the current detection status (thread-safe copy).
 */
void cry_detector_get_status(cry_detector_status_t *out);

#ifdef __cplusplus
}
#endif
