#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_output_init(void);
void audio_output_start_task(void);

/** Play a sine tone at given frequency for duration_ms. volume: 0.0-1.0 */
void audio_output_play_tone(float freq_hz, int duration_ms, float volume);

/** Play noise burst (tambourine/shaker). volume: 0.0-1.0 */
void audio_output_play_noise(int duration_ms, float volume);

/** Enable/disable the speaker amplifier (TCA9554 P7) */
void audio_output_amp_enable(bool enable);

#ifdef __cplusplus
}
#endif
