---
name: nursery-rhyme-music
description: "Music theory copilot for the ESP32 nursery rhyme player (project 15_nursery_rhymes). Handles melody transcription, rhythm validation, song data format, and the Kconfig build toggle system. Use this skill whenever modifying files under projects/15_nursery_rhymes/, when the user mentions nursery rhyme songs, melody notes, music transcription, song accuracy, rhythm checking, adding new songs, or fixing note data. Also trigger when the user references song_data.c, note durations, MIDI notes, or music theory in the context of this project."
---

# Nursery Rhyme Music Theory Copilot

You are a music theory copilot for an ESP32-C6 nursery rhyme player that synthesizes melodies through a tiny speaker. Your job is to ensure every note transcription is rhythmically correct, melodically accurate, and playable on the hardware.

## Golden Rule

**Never generate melodies from memory.** AI-generated music transcriptions are unreliable — they get the general shape right but botch specific intervals and rhythms. Always verify against published sheet music. Reference sources are catalogued in `docs/reference/nursery_rhyme_scores.md`.

When adding or fixing songs:
1. Search the web for the standard sheet music (8notes.com, musescore.com, makingmusicfun.net, bethsnotesplus.com)
2. Cross-reference at least two sources
3. Transcribe note-by-note from the source
4. Run the audit script to verify measure totals

## Project Layout

```
projects/15_nursery_rhymes/
├── main/
│   ├── song_data.c          # All melody note arrays + song table
│   ├── song_data.h          # note_t/song_t structs, MIDI defines, duration constants
│   ├── audio_player.c       # Playback engine (transpose, ADSR, loop/shuffle)
│   ├── audio_player.h       # play_mode_t enum, public API
│   ├── ui.c                 # LVGL touch UI (song list + now-playing)
│   ├── main.c               # Init sequence, LVGL task
│   └── Kconfig.projbuild    # Per-song compile-time toggles
├── sdkconfig.defaults
├── partitions.csv
└── CMakeLists.txt
docs/reference/
└── nursery_rhyme_scores.md  # Verified source URLs for all songs
```

## Note Encoding

### MIDI Note Names (defined in song_data.h)

| Note | MIDI | Note | MIDI | Note | MIDI |
|------|------|------|------|------|------|
| C3=48 | D3=50 | E3=52 | F3=53 | G3=55 | A3=57 |
| B3=59 | C4=60 | D4=62 | E4=64 | F4=65 | G4=67 |
| A4=69 | B4=71 | C5=72 | D5=74 | E5=76 | F5=77 |
| G5=79 | A5=81 | B5=83 | C6=84 | REST=0 | |

Sharps/flats available: Cs4=61, Eb4=63, Fs4=66, Gs4=68, Bb4=70, etc.

### Duration Constants (at 120 BPM baseline, quarter = 500ms)

| Symbol | Name | Beats | Ms |
|--------|------|-------|-----|
| W | Whole | 4 | 2000 |
| DH | Dotted Half | 3 | 1500 |
| H | Half | 2 | 1000 |
| DQ | Dotted Quarter | 1.5 | 750 |
| Q | Quarter | 1 | 500 |
| DE | Dotted Eighth | 0.75 | 375 |
| E | Eighth | 0.5 | 250 |
| S | Sixteenth | 0.25 | 125 |

### Data Structures

```c
typedef struct {
    uint8_t  midi_note;   /* 0 = rest, 48-84 = C3-C6 */
    uint16_t duration_ms; /* note duration in ms */
} note_t;

typedef struct {
    const char   *title;
    const char   *lyrics_short;  /* first line for display */
    uint16_t      tempo_bpm;
    uint16_t      time_sig_num;  /* e.g. 4 for 4/4 */
    uint16_t      time_sig_den;  /* e.g. 4 for 4/4 */
    const note_t *notes;
    uint16_t      note_count;
} song_t;
```

### Song Table Pattern

```c
#define SONG_ENTRY(title_str, lyrics_str, bpm, ts_n, ts_d, arr) \
    { title_str, lyrics_str, bpm, ts_n, ts_d, arr, sizeof(arr)/sizeof(arr[0]) }

#define SONG_ON(cfg) (defined(CONFIG_SONG_INCLUDE_ALL) || defined(cfg))

const song_t g_songs[] = {
#if SONG_ON(CONFIG_SONG_TWINKLE)
    SONG_ENTRY("Twinkle Twinkle", "Twinkle twinkle little star", 100, 4, 4, twinkle_notes),
#endif
    // ...
};
```

## Rhythm Validation

### Measure Durations

Every song's total note duration must be an exact multiple of its measure length:

| Time Sig | Beats/Bar | Ms/Bar | Grouping |
|----------|-----------|--------|----------|
| 4/4 | 4 | 2000 | 4 quarter notes |
| 3/4 | 3 | 1500 | 3 quarter notes |
| 6/8 | 2 dotted quarters | 1500 | 2 groups of 3 eighths |

**The one exception**: pickup notes (anacrusis). If a song starts before beat 1 (e.g., "Hick-" in Hickory Dickory), the first partial bar + last partial bar together should equal one full bar. The total will show as N.5 bars — that's correct.

### Running the Audit

Use the Python audit script at `scripts/audit_songs.py` (bundled with this skill) or run inline:

```bash
python3 /path/to/audit_songs.py
```

It parses song_data.c, extracts note arrays, computes total duration per song, and flags any that don't sum to whole measures. Songs with pickups show as X.5 bars — verify these manually.

### Common Rhythm Errors

1. **Missing rests between phrases** — a phrase ending mid-bar needs a rest to complete the bar
2. **Pickup notes at song end** — if verse 1 has a pickup, don't add the pickup again at the very end (it belongs to the next verse which doesn't exist)
3. **Wrong note duration for time signature** — a dotted quarter (DQ) in 4/4 leaves an uneven remainder; use Q+E or H instead
4. **Phrase != bar** — a lyric phrase can span 1.5 or 2.5 bars. That's fine. Only the *total* must be a whole number of bars

## Playback Characteristics

The audio engine applies these transforms at playback time (do NOT bake them into song_data.c):

- **Transpose**: `TRANSPOSE_SEMITONES = 12` — all notes shifted up one octave. Song data is written at concert pitch (C4=middle C), played back at C5. This is because the NS4150B tiny speaker has poor bass response; C5 range (523+ Hz) is its sweet spot.
- **ADSR envelope**: 15ms attack, sustain, 30ms release — prevents clicks between notes
- **Sample rate**: 16kHz mono, output as 16-bit stereo through ES8311 DAC
- **Inter-note gap**: 15ms silence between consecutive notes for articulation
- **Inter-song gap**: 250ms silence between auto-advancing songs

## Adding a New Song

### Step 1: Find Verified Sheet Music

Search for the melody on these sites (prioritize free lead sheets):
- 8notes.com, musescore.com, makingmusicfun.net, bethsnotesplus.com
- Cross-check at least 2 sources

Add the source URL to `docs/reference/nursery_rhyme_scores.md`.

### Step 2: Transcribe the Note Array

Write the `static const note_t` array in song_data.c, above the song table. Follow these conventions:
- All songs in C major (transpose from source if needed)
- Comment each lyric phrase above its notes
- Use the shortest note values that accurately capture the rhythm
- Include rests where the melody pauses

```c
static const note_t new_song_notes[] = {
    /* First lyric phrase */
    {C4, Q}, {D4, Q}, {E4, Q}, {F4, Q},
    /* Second lyric phrase */
    {G4, H}, {E4, H},
};
```

### Step 3: Add to Song Table

Add a `SONG_ENTRY` line inside `#if SONG_ON(CONFIG_SONG_XXX)`:

```c
#if SONG_ON(CONFIG_SONG_NEW_SONG)
    SONG_ENTRY("New Song", "First line of lyrics", 120, 4, 4, new_song_notes),
#endif
```

### Step 4: Add Kconfig Toggle

Add to `main/Kconfig.projbuild`:

```
    config SONG_NEW_SONG
        bool "N. New Song Title"
        default y
        depends on !SONG_INCLUDE_ALL
```

### Step 5: Validate

Run the audit script. The new song must show as N.00 measures (or N.50 if it has a pickup).

### Step 6: Build and Listen

```bash
idf.py -C projects/15_nursery_rhymes build
idf.py -C projects/15_nursery_rhymes -p /dev/cu.usbmodemXXXX flash
```

Remember: delete `sdkconfig` after changing Kconfig to force regeneration.

## Kconfig Song Toggle System

`CONFIG_SONG_INCLUDE_ALL=y` (default) includes every song. To exclude individual songs:
1. `idf.py -C projects/15_nursery_rhymes menuconfig`
2. Navigate to "Nursery Rhyme Song Selection"
3. Uncheck "Include all songs"
4. Toggle individual songs

Or set in `sdkconfig.defaults`:
```
CONFIG_SONG_INCLUDE_ALL=n
CONFIG_SONG_TWINKLE=y
CONFIG_SONG_MARY=n
# etc.
```

After changing: delete `sdkconfig` and rebuild.

## Music Theory Quick Reference

### Intervals in C Major

| Interval | Semitones | Example |
|----------|-----------|---------|
| Unison | 0 | C→C |
| Major 2nd | 2 | C→D |
| Major 3rd | 4 | C→E |
| Perfect 4th | 5 | C→F |
| Perfect 5th | 7 | C→G |
| Major 6th | 9 | C→A |
| Major 7th | 11 | C→B |
| Octave | 12 | C4→C5 |

### Checking Melodic Accuracy

When verifying a transcription, sing or hum the melody and check:
1. **Starting note** — most C major nursery rhymes start on C4, E4, or G4
2. **Highest note** — rarely above C5 for simple melodies (A5 for Itsy Bitsy Spider is an outlier)
3. **Cadence** — almost all end on C4 (tonic). If it ends on another note, double-check
4. **Leaps** — intervals larger than a 5th (7 semitones) are rare. Flag any octave jumps
5. **Accidentals** — nursery rhymes in C major almost never use sharps/flats. If you see Fs4, Bb4, etc., verify carefully
