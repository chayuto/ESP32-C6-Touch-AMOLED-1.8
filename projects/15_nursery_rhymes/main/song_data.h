#pragma once

#include <stdint.h>
#include <stddef.h>

/*
 * Song data for nursery rhyme player.
 *
 * Each note is encoded as a (midi_note, duration_ms) pair.
 * midi_note=0 means rest. Duration is in milliseconds at the song's BPM.
 * Frequencies are computed from MIDI note numbers at playback time.
 *
 * MIDI note reference:
 *   C4=60, D4=62, E4=64, F4=65, G4=67, A4=69, B4=71
 *   C5=72, D5=74, E5=76, F5=77, G5=79, A5=81, B5=83
 */

/* Duration constants (at 120 BPM, quarter = 500ms) */
#define W   2000   /* whole */
#define H   1000   /* half */
#define DH  1500   /* dotted half */
#define Q    500   /* quarter */
#define DQ   750   /* dotted quarter */
#define E    250   /* eighth */
#define DE   375   /* dotted eighth */
#define S    125   /* sixteenth */

/* MIDI note names */
#define REST  0

#define C3   48
#define D3   50
#define E3   52
#define F3   53
#define G3   55
#define A3   57
#define B3   59

#define C4   60
#define Cs4  61
#define D4   62
#define Eb4  63
#define E4   64
#define F4   65
#define Fs4  66
#define G4   67
#define Gs4  68
#define A4   69
#define Bb4  70
#define B4   71

#define C5   72
#define Cs5  73
#define D5   74
#define Eb5  75
#define E5   76
#define F5   77
#define Fs5  78
#define G5   79
#define Gs5  80
#define A5   81
#define Bb5  82
#define B5   83

#define C6   84

typedef struct {
    uint8_t  midi_note;   /* 0 = rest, 48-84 = C3-C6 */
    uint16_t duration_ms; /* note duration in ms */
} note_t;

typedef struct {
    const char   *title;
    const char   *lyrics_short;  /* first line for display */
    uint16_t      tempo_bpm;     /* original tempo */
    uint16_t      time_sig_num;  /* e.g. 4 for 4/4 */
    uint16_t      time_sig_den;  /* e.g. 4 for 4/4 */
    const note_t *notes;
    uint16_t      note_count;
} song_t;

/* All songs array */
extern const song_t g_songs[];
extern const int    g_song_count;

/* Get frequency in Hz from MIDI note (0 = silence) */
float midi_to_freq(uint8_t midi_note);
