/*
 * audio_jingles.c — short event sounds, synthesised on the fly.
 *
 * Each jingle is a short sequence of tones / noise bursts queued through the
 * audio_output module. Jingles are best-effort: if audio init failed, calls
 * are no-ops. Keep them under ~2 seconds so they don't block care actions.
 */

#include "audio_jingles.h"
#include "audio_output.h"
#include "esp_log.h"

static const char *TAG = "jingles";

/* Note frequencies (equal temperament, A4 = 440 Hz) */
#define NOTE_C4  261.63f
#define NOTE_E4  329.63f
#define NOTE_G4  392.00f
#define NOTE_A4  440.00f
#define NOTE_C5  523.25f
#define NOTE_D5  587.33f
#define NOTE_E5  659.25f
#define NOTE_F5  698.46f
#define NOTE_G5  783.99f
#define NOTE_A5  880.00f
#define NOTE_B5  987.77f
#define NOTE_C6  1046.50f

void audio_jingles_play(jingle_t j)
{
    switch (j) {
    case JINGLE_HATCH:
        audio_output_play_tone(NOTE_C5, 120, 0.5f);
        audio_output_play_tone(NOTE_E5, 120, 0.5f);
        audio_output_play_tone(NOTE_G5, 120, 0.5f);
        audio_output_play_tone(NOTE_C6, 240, 0.5f);
        break;

    case JINGLE_EAT:
        audio_output_play_tone(NOTE_E5, 80, 0.4f);
        audio_output_play_tone(NOTE_G5, 80, 0.4f);
        audio_output_play_tone(NOTE_E5, 80, 0.4f);
        break;

    case JINGLE_HAPPY:
        audio_output_play_tone(NOTE_C5, 90, 0.5f);
        audio_output_play_tone(NOTE_E5, 90, 0.5f);
        audio_output_play_tone(NOTE_G5, 90, 0.5f);
        audio_output_play_tone(NOTE_C6, 180, 0.5f);
        break;

    case JINGLE_SAD:
        audio_output_play_tone(NOTE_E5, 200, 0.4f);
        audio_output_play_tone(NOTE_C5, 300, 0.4f);
        break;

    case JINGLE_POOP:
        audio_output_play_noise(150, 0.4f);
        break;

    case JINGLE_SICK:
        audio_output_play_tone(NOTE_E5, 200, 0.3f);
        audio_output_play_tone(NOTE_D5, 200, 0.3f);
        audio_output_play_tone(NOTE_C5, 400, 0.3f);
        break;

    case JINGLE_DEATH:
        audio_output_play_tone(NOTE_C5, 300, 0.5f);
        audio_output_play_tone(NOTE_A4, 300, 0.5f);
        audio_output_play_tone(NOTE_G4, 400, 0.5f);
        audio_output_play_tone(NOTE_E4, 600, 0.5f);
        break;

    case JINGLE_LEVELUP:
        audio_output_play_tone(NOTE_C5, 100, 0.5f);
        audio_output_play_tone(NOTE_E5, 100, 0.5f);
        audio_output_play_tone(NOTE_G5, 100, 0.5f);
        audio_output_play_tone(NOTE_C6, 100, 0.5f);
        audio_output_play_tone(NOTE_E5, 100, 0.5f);
        audio_output_play_tone(NOTE_G5, 100, 0.5f);
        audio_output_play_tone(NOTE_C6, 300, 0.5f);
        break;

    case JINGLE_DISCIPLINE:
        audio_output_play_tone(NOTE_A5, 80, 0.5f);
        audio_output_play_tone(NOTE_F5, 200, 0.5f);
        break;

    default:
        ESP_LOGW(TAG, "unknown jingle %d", (int)j);
        break;
    }
}
