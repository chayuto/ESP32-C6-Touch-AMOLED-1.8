#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NOISE_VOICE_WHITE = 0,
    NOISE_VOICE_PINK,
    NOISE_VOICE_BROWN,
    NOISE_VOICE_WOMB,
    NOISE_VOICE_COUNT,
} noise_voice_t;

/* Timer presets, minutes. 0xFFFF = continuous. */
#define NOISE_TIMER_INF  0xFFFF

/** Initialize generator state and start the noise task. Call after
 *  audio_player_init() — the noise generator shares I2S with the song player. */
esp_err_t noise_player_init(void);

/** Start playback. Yields I2S from the song player; fade-in happens in-task. */
void noise_player_play(noise_voice_t voice, uint16_t timer_min);

/** Stop with fade-out. Idempotent. */
void noise_player_stop(void);

/** Live: change the active voice (hard swap in Phase 1, cross-faded in Phase 3). */
void noise_player_set_voice(noise_voice_t voice);

/** Set output gain 0..100 (clamped to CONFIG_NOISE_VOL_CAP). */
void noise_player_set_volume(uint8_t vol);

/** Restart the timer with `min` minutes (or NOISE_TIMER_INF). */
void noise_player_set_timer(uint16_t min);

bool          noise_player_is_playing(void);
/** True while a fade-out is in progress (user stop or timer-expired stop). */
bool          noise_player_is_stopping(void);
noise_voice_t noise_player_get_voice(void);
uint8_t       noise_player_get_volume(void);
uint16_t      noise_player_get_timer_remaining_sec(void);

const char *noise_player_voice_name(noise_voice_t v);

#ifdef __cplusplus
}
#endif
