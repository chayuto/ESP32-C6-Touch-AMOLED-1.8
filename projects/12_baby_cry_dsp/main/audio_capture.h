#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize ES8311 codec via esp_codec_dev and I2S for 16kHz/16-bit mono capture.
 * Uses the I2C bus handle from amoled_get_i2c_bus().
 */
esp_err_t audio_capture_init(void);

/**
 * Read audio samples from the ring buffer.
 * Blocks until the requested number of samples is available.
 * @param buf      Output buffer for 16-bit PCM samples
 * @param samples  Number of samples to read
 * @return Number of samples actually read
 */
size_t audio_capture_read(int16_t *buf, size_t samples);

/**
 * Get the current RMS energy level (updated by the capture task).
 * Returns a value 0-32767.
 */
int16_t audio_capture_get_rms(void);

/**
 * Start the audio capture task (continuously fills ring buffer).
 */
void audio_capture_start_task(void);

#ifdef __cplusplus
}
#endif
