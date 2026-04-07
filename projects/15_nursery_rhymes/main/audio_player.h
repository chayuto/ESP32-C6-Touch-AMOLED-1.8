#pragma once

#include "esp_err.h"
#include <stdbool.h>
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

/** Cycle to next play mode (OFF -> LOOP_ALL -> LOOP_ONE -> SHUFFLE -> OFF). */
void audio_player_cycle_mode(void);

/** Set play mode directly. */
void audio_player_set_mode(play_mode_t mode);

/** Get current play mode. */
play_mode_t audio_player_get_mode(void);

#ifdef __cplusplus
}
#endif
