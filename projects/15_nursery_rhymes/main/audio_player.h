#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize I2S + ES8311 codec. Call after amoled_init(). */
esp_err_t audio_player_init(void);

/** Start the audio playback background task. */
void audio_player_start_task(void);

/** Enable/disable the NS4150B speaker amplifier (TCA9554 P7). */
void audio_player_amp_enable(bool enable);

/** Playback repeat/shuffle modes. */
typedef enum {
    PLAY_MODE_OFF = 0,   /* Stop after current song */
    PLAY_MODE_LOOP_ALL,  /* Play all songs sequentially, repeat */
    PLAY_MODE_LOOP_ONE,  /* Repeat current song */
    PLAY_MODE_SHUFFLE,   /* Random order, no immediate repeat */
    PLAY_MODE_COUNT,
} play_mode_t;

/** Start playing a song by index. Stops any currently playing song. */
void audio_player_play(int song_index);

/** Stop playback. */
void audio_player_stop(void);

/** Returns true if currently playing. */
bool audio_player_is_playing(void);

/** Returns current song index (-1 if none). */
int audio_player_current_song(void);

/** Returns progress 0-100. */
int audio_player_progress(void);

/** Set volume 0-100. */
void audio_player_set_volume(int vol);

/** Get volume 0-100. */
int audio_player_get_volume(void);

/** Returns true if a song is queued to play next (pending start). */
bool audio_player_is_song_queued(void);

/** Returns current note index (0-based). */
int audio_player_note_index(void);

/** Cycle to next play mode (OFF -> LOOP_ALL -> LOOP_ONE -> SHUFFLE -> OFF). */
void audio_player_cycle_mode(void);

/** Set play mode directly. */
void audio_player_set_mode(play_mode_t mode);

/** Get current play mode. */
play_mode_t audio_player_get_mode(void);

/** Pause the song player and release I2S so the noise generator can drive it.
 *  Aborts the current song (sets s_stop_req) and waits up to 100 ms for the
 *  audio task to leave its playback path. After this returns, the audio task
 *  yields its idle-silence loop so noise_player can write to I2S. */
void audio_player_yield_to_noise(void);

/** Resume the audio task's idle-silence loop after noise stops. Idempotent. */
void audio_player_resume_from_noise(void);

/** Returns true if the audio task is currently yielding I2S to the noise generator. */
bool audio_player_is_yielded_to_noise(void);

/** Stereo-write helper used by the noise generator. The noise task is the sole
 *  I2S writer while audio_player_is_yielded_to_noise() is true. */
void audio_player_write_stereo(const int16_t *buf, size_t stereo_samples);

#ifdef __cplusplus
}
#endif
