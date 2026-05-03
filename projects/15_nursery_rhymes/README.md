# Nursery Rhymes Player

A music-box nursery rhyme player running on the Waveshare ESP32-C6-Touch-AMOLED-1.8. Tap a song on the 1.8" AMOLED touchscreen and hear it played through the onboard speaker with realistic music-box timbre, proper tempo, metric accents, and musical expression.

20 classic public-domain nursery rhymes, all hand-transcribed from published sheet music and validated with automated rhythm auditing.

## Demo

- Scrollable song list with tap-to-play
- Now Playing screen with progress bar, volume controls, play mode toggle, and battery gauge
- BOOT button: short press = brightness cycle (off/dim/med/bright), long press = child lock

## Music Theory in Code

This project demonstrates how music theory principles translate directly into embedded audio synthesis. Every musical concept is implemented in `audio_player.c`:

### Harmonic-Rich Timbre

Pure sine waves sound sterile and electronic. Real instruments produce **overtones** — integer multiples of the fundamental frequency. The playback engine mixes four harmonics to create a music-box / glockenspiel timbre:

```
Fundamental (1st harmonic): 55% amplitude
2nd harmonic (octave):      25%
3rd harmonic (fifth):       13%
4th harmonic (2 octaves):    7%
```

This simple additive synthesis creates a warm, bell-like tone that sounds musical rather than clinical, especially on the tiny NS4150B Class-D speaker amplifier.

### Tempo-Aware Playback

Song data is encoded at a reference tempo of 120 BPM (quarter note = 500ms). At playback time, durations are scaled by `120 / actual_bpm`:

| Song | Marked BPM | Scale Factor | Feel |
|------|-----------|-------------|------|
| Rock-a-Bye Baby | 80 | 1.50x (slower) | Gentle lullaby waltz |
| Twinkle Twinkle | 100 | 1.20x | Calm, singing pace |
| Mary Had a Lamb | 120 | 1.00x | Standard tempo |

This means the same data format works for all tempos — no re-encoding needed.

### Metric Accent (Strong/Weak Beats)

In music, not all beats are equal. Beat 1 of each measure is the **downbeat** (strongest), and other beats have varying strength depending on the time signature. The engine tracks beat position within each measure and adjusts volume:

| Beat Position | Accent | Volume |
|--------------|--------|--------|
| Beat 1 (downbeat) | Strong | 100% |
| Beat 3 in 4/4, Beat 4 in 6/8 | Medium | 75% |
| All other beats | Weak | 55% |

This creates the natural rhythmic "pulse" that makes music feel alive — a waltz (3/4) lilts differently than a march (4/4).

### Duration-Proportional ADSR Envelope

Every note has an attack-decay-sustain-release envelope, but the shape adapts to note length:

- **Attack**: 3% of duration (clamped 5–40ms), quadratic ease-in for soft onset
- **Release**: 8% of duration (clamped 15–80ms), cosine fade for smooth cutoff
- **Articulation gap**: 12% silence at note end for legato-but-separated phrasing

Short eighth notes (250ms) get a 7ms attack — snappy and percussive. Long half notes (1000ms) get a 30ms attack — gentle and singing. This mirrors how real instruments behave.

### Vibrato

Sustained notes (>300ms) get gentle vibrato — a 4.5 Hz pitch wobble of ±0.3% that fades in after 200ms. This adds warmth and prevents long tones from sounding static, mimicking the natural instability of acoustic instruments.

### Rallentando

The final 3 notes of every song slow down progressively (up to 15%), creating the natural "winding down" that musicians instinctively add at the end of a piece.

### Octave Transposition

All melodies are transcribed at concert pitch (C4 = middle C, ~262 Hz) matching standard sheet music. At playback, a +12 semitone transpose shifts everything to C5 (~523 Hz) — the sweet spot for the NS4150B speaker, which has poor bass response below 400 Hz. Children's songs are naturally sung in this register.

## Rhythm Validation

Every song's note durations are verified to sum to complete measures:

```
$ python3 .claude/skills/nursery-rhyme-music/scripts/audit_songs.py

   1. Twinkle Twinkle Little Star          42 notes   24000ms  PASS (12 bars)
   2. Mary Had a Little Lamb               26 notes   16000ms  PASS (8 bars)
  ...
  10. Hickory Dickory Dock                 27 notes   11250ms  PASS (7.5 bars, anacrusis)
  ...
  20 songs checked, 0 errors
```

The 0.5-bar count for Hickory Dickory is correct — the "Hick-" pickup note (anacrusis) accounts for the half-bar offset.

## Song List

| # | Song | BPM | Time Sig |
|---|------|-----|----------|
| 1 | Twinkle Twinkle Little Star | 100 | 4/4 |
| 2 | Mary Had a Little Lamb | 120 | 4/4 |
| 3 | Baa Baa Black Sheep | 110 | 4/4 |
| 4 | London Bridge Is Falling Down | 120 | 4/4 |
| 5 | Row Row Row Your Boat | 100 | 6/8 |
| 6 | Jack and Jill | 108 | 6/8 |
| 7 | Humpty Dumpty | 120 | 3/4 |
| 8 | Itsy Bitsy Spider | 120 | 6/8 |
| 9 | Old MacDonald Had a Farm | 120 | 4/4 |
| 10 | Hickory Dickory Dock | 120 | 6/8 |
| 11 | Three Blind Mice | 108 | 6/8 |
| 12 | Hot Cross Buns | 100 | 4/4 |
| 13 | Frere Jacques | 120 | 4/4 |
| 14 | Yankee Doodle | 120 | 4/4 |
| 15 | Pop Goes the Weasel | 120 | 6/8 |
| 16 | Rock-a-Bye Baby | 80 | 3/4 |
| 17 | Hush Little Baby | 100 | 4/4 |
| 18 | Wheels on the Bus | 120 | 4/4 |
| 19 | Head Shoulders Knees and Toes | 120 | 4/4 |
| 20 | If You're Happy and You Know It | 120 | 4/4 |

All melodies are public domain. Source references in [`docs/reference/nursery_rhyme_scores.md`](../../docs/reference/nursery_rhyme_scores.md).

## Playback Modes

Tap the mode button on the Now Playing screen to cycle:

| Mode | Behavior |
|------|----------|
| Play Once | Stop after current song (default) |
| Loop All | Play all songs sequentially, repeat from start |
| Loop One | Repeat current song indefinitely |
| Shuffle | Random order, no immediate repeat (Fisher-Yates) |

250ms silence gap between auto-advancing songs.

## White Noise Sleep Generator

A continuous-playback noise generator for soothing babies and helping
toddlers fall asleep. Tap the **White Noise** row at the top of the
song list to open the dedicated player.

### Voices

Four real-time-synthesised voices (no pre-recorded audio — all generated
from algorithms, ~290 LoC):

| Voice | Algorithm | Character |
|-------|-----------|-----------|
| **White** | xorshift32, top 16 bits | Bright, broadband — like static |
| **Pink** | Voss-McCartney 7-row | Equal energy per octave — calmer than white |
| **Brown** | leaky-integrator 1-pole IIR (α=0.99) | Deep low-end — like rain on a roof |
| **Womb** | Brown × 4 Hz LFO + 70 BPM heartbeat | Mimics in-utero soundscape for newborns |

Voices are calibrated to roughly match each other's RMS at full volume
(within ~3 dB), and the volume slider runs 0..100 of `CONFIG_NOISE_VOL_CAP`
(default 80%, raise to 100% in menuconfig if your speaker is quieter).

### Auto-off timer

`15 / 30 / 60 / 120 min` or **continuous** (default). When a preset timer
expires, the engine performs a 12-second gentle fade-out so a sleeping
baby isn't startled by an abrupt cut. The stop button uses a snappier
1.5-second fade — different intent, different ramp.

### Audio routing

The noise engine and song player are mutually exclusive: starting noise
yields I2S from the song player; tapping a song stops noise (with fade)
and starts the song. The song stop button also got a small 80 ms
cosine fade-out as part of this work, so neither player produces a
truncation pop on stop.

### AAP guidance

The American Academy of Pediatrics recommends infant sleep environments
stay below 50 dB and that sound machines auto-off after the baby falls
asleep. The default volume cap (80%) and timer presets are aimed at that
guidance — but the firmware lets you choose a continuous timer if you
want, and the volume cap is a Kconfig knob. Measure SPL at the bedside
once before relying on it overnight.

See `docs/WHITE_NOISE_RESEARCH.md` and `docs/WHITE_NOISE_IMPLEMENTATION_PLAN.md`
for the full audio + safety + engineering rationale.

## Build Configuration

### Compile-Time Song Selection

All 20 songs are included by default. To exclude individual songs:

```bash
idf.py -C projects/15_nursery_rhymes menuconfig
# Navigate to: Nursery Rhyme Song Selection
# Uncheck "Include all songs"
# Toggle individual songs on/off
```

### Build & Flash

```bash
. ~/esp/esp-idf/export.sh
idf.py -C projects/15_nursery_rhymes build
idf.py -C projects/15_nursery_rhymes -p /dev/cu.usbmodem3101 flash
```

## Architecture

```
audio_player.c     Synthesis engine: harmonics, ADSR, vibrato, tempo scaling
                   Playback task: sequencer, loop/shuffle, rallentando
song_data.c        20 note arrays + song table with Kconfig toggles
song_data.h        note_t/song_t structs, MIDI defines, duration constants
ui.c               LVGL UI: song list, now-playing, child lock, battery
main.c             Init sequence, LVGL task, button handling
Kconfig.projbuild  Per-song compile-time toggle menu
```

## Hardware Used

- **Speaker**: NS4150B Class-D amplifier, enabled via TCA9554 IO expander P7
- **DAC**: ES8311 codec, I2S master mode, 16kHz 16-bit stereo
- **Display**: SH8601 1.8" AMOLED, 368x448 QSPI
- **Touch**: FT3168 capacitive, I2C
- **Power**: AXP2101 PMIC with battery gauge on Now Playing screen
