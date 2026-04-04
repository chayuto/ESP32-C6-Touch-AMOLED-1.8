#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** VAD detection result */
typedef enum {
    VAD_RESULT_SILENCE = 0,  /**< No voice activity detected */
    VAD_RESULT_SPEECH,       /**< Voice activity detected */
} vad_result_t;

/** VAD implementation type */
typedef enum {
    VAD_IMPL_ENERGY = 0,     /**< Energy-based VAD (fallback) */
    VAD_IMPL_ESP_SR,         /**< ESP-SR VADNet (if available) */
} vad_impl_t;

/**
 * Initialize the VAD filter.
 * Attempts to use ESP-SR VADNet first, falls back to energy-based VAD.
 * @return ESP_OK on success
 */
esp_err_t vad_filter_init(void);

/**
 * Process a frame of audio through the VAD.
 * @param samples  Pointer to 16-bit PCM samples (16kHz mono)
 * @param count    Number of samples (should be 480 for 30ms frame)
 * @return VAD_RESULT_SPEECH if voice detected, VAD_RESULT_SILENCE otherwise
 */
vad_result_t vad_filter_process(const int16_t *samples, size_t count);

/**
 * Get which VAD implementation is active.
 */
vad_impl_t vad_filter_get_impl(void);

/**
 * Get the current VAD state (thread-safe).
 */
bool vad_filter_is_speech(void);

#ifdef __cplusplus
}
#endif
