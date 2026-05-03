# White Noise — Technical Implementation Plan

Engineering plan for adding a continuous-playback white-noise generator to
`projects/15_nursery_rhymes`. Companion to `WHITE_NOISE_RESEARCH.md`
(audio + safety) and `WHITE_NOISE_UI_PLAN.md` (UI/UX).

This document specifies file structure, public APIs, the audio task state
machine, integration with the existing song player, build/Kconfig changes,
RAM/flash budget, and a phased rollout. **No code is changed in this
session — this is a plan.**

---

## 1. Scope

### In scope (v1)
- 4 noise voices: White, Pink, Brown, Womb (brown + 4 Hz mod + heartbeat)
- White-noise row on the song list (entry point)
- New full-screen "White Noise" view with play/pause, voice picker,
  volume slider, timer
- Continuous synthesis (no pre-recorded audio)
- Linear fade-in (3 s) and fade-out (12 s)
- Auto-off timer: 15 / 30 / 60 / 120 min / ∞
- NVS persistence of voice / volume / timer
- Coexistence with existing song playback (mutual exclusion)

### Stretch (v1.5 / v2)
- Three flavour voices: Rain, Fan, Ocean
- Heartbeat toggle inside Womb voice
- Per-voice volume offsets (calibration)
- Cross-fade between voices (currently 250 ms hard-ish swap)
- Fade-to-screen-off after timer expiry

### Out of scope
- Pre-recorded audio (would require flash storage and a decoder; the
  generators are good enough and far cheaper)
- Microphone-based environmental noise matching
- BT speaker output

---

## 2. File and module changes

### New files

```
projects/15_nursery_rhymes/main/
├── noise_player.c     ← new generator + task + fade engine
├── noise_player.h     ← public API
└── ui_noise.c         ← new view (white-noise full-screen)
```

`ui_noise.c` is split out from `ui.c` to keep `ui.c` from ballooning past
its current 538 lines. It exposes a small API used by `ui.c` to construct
the view and toggle visibility.

### Modified files

| File | Change |
|------|--------|
| `main/ui.h`              | Add `ui_show_noise_view()`, `ui_show_song_list()` helpers (existing show/hide logic moves out) |
| `main/ui.c`              | Insert white-noise row at top of song list; route taps to new view; remove direct manipulation in favour of helper API |
| `main/main.c`            | Call `noise_player_init()` after `audio_player_init()`; pass codec handle |
| `main/audio_player.c`    | **Tiny change**: add `audio_player_pause_for_noise(bool)` to release the I2S TX gracefully so noise can take over. Alternative: add a single output mixer (see §4 below) |
| `main/audio_player.h`    | Add the new function above |
| `main/CMakeLists.txt`    | Add `noise_player.c`, `ui_noise.c` to SRCS |
| `main/Kconfig.projbuild` | New menu "White Noise Generator" with toggles per voice (compile-time exclusion to save flash) |
| `sdkconfig.defaults`     | Add `CONFIG_LV_FONT_MONTSERRAT_14=y` if not already on (used by voice subtitle); audit confirms it's needed |
| `README.md`              | New section documenting the feature, safety guidance, and mention of AAP guideline |
| `docs/WHITE_NOISE_*.md`  | the three planning docs (this file lives here) |

### Estimated diff size

- ~600 lines added to `noise_player.c` (engine + task + fades + heartbeat)
- ~350 lines added to `ui_noise.c` (view construction + event callbacks)
- ~80 lines changed across `ui.c` / `main.c` / `audio_player.c`
- Total: ~1000 LoC. Comparable to existing `audio_player.c` (644 lines).

---

## 3. Public API

### `noise_player.h`

```c
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    NOISE_VOICE_WHITE = 0,
    NOISE_VOICE_PINK,
    NOISE_VOICE_BROWN,
    NOISE_VOICE_WOMB,
    /* v1.5 */
    NOISE_VOICE_RAIN,
    NOISE_VOICE_FAN,
    NOISE_VOICE_OCEAN,
    NOISE_VOICE_COUNT,
} noise_voice_t;

/* Timer presets, minutes. 0xFFFF = continuous. */
#define NOISE_TIMER_INF  0xFFFF

/** Initialize generator state. Call after audio_player_init(). */
esp_err_t noise_player_init(void);

/** Start playback. Stops any song that is currently playing. Returns
 *  immediately; fade-in happens in the audio task. */
void noise_player_play(noise_voice_t voice, uint16_t timer_min);

/** Stop with fade-out. Idempotent. */
void noise_player_stop(void);

/** Live: change the active voice (cross-faded). */
void noise_player_set_voice(noise_voice_t voice);

/** Set output gain 0..80 (units = percent). Capped at 80. */
void noise_player_set_volume(uint8_t vol);

/** Restart the timer with `min` minutes (or NOISE_TIMER_INF). */
void noise_player_set_timer(uint16_t min);

/** Queries used by the UI tick. */
bool          noise_player_is_playing(void);
noise_voice_t noise_player_get_voice(void);
uint8_t       noise_player_get_volume(void);
uint16_t      noise_player_get_timer_remaining_sec(void);  /* 0 = expired */

/** Persistence (called from view exit). */
void noise_player_save_prefs(void);
void noise_player_load_prefs(void);
```

### Changes to `audio_player.h`

```c
/** Pause the song player and release I2S so noise can drive it.
 *  Returns immediately. The current song is aborted (s_stop_req=true). */
void audio_player_yield_to_noise(void);

/** Resume the song player after noise stops. Idempotent if already ready. */
void audio_player_resume_from_noise(void);
```

These are zero-cost wrappers around the existing `s_stop_req` and the audio
task's idle silence loop.

### Changes to `ui.h`

```c
void ui_show_song_list(void);
void ui_show_noise_view(void);
void ui_show_now_playing(void);   /* called by audio_player on song start */
```

`ui_update()` continues to be the single timer-driven update entry point;
when the noise view is visible it routes to `ui_noise_update()`.

---

## 4. Audio routing — single producer at a time

The existing `audio_player.c` owns the I2S TX channel and the codec handle.
It is *the* writer. Two design options:

### Option A — exclusive ownership swap (recommended for v1)

`audio_player_yield_to_noise()` does:
1. Set `s_stop_req = true`. The song task stops writing within ~30 ms.
2. Wait for `s_playing` to clear (poll, max 100 ms).
3. Set a new flag `s_yielded_to_noise = true`. While set, the song task's
   idle-silence loop *exits* instead of writing zeros.

`noise_player`'s task then writes to I2S directly. When noise stops,
`audio_player_resume_from_noise()` clears the flag, song task resumes its
silence loop.

Pros: no shared-state hazards, smallest diff.
Cons: ownership swap takes ~30 ms (silent gap during transitions).

### Option B — software mixer

Add a tiny mixer: both producers write to per-source ring buffers, a single
"audio output" task reads from both and sums. Allows simultaneous song +
noise (whisper a lullaby over rain).

Pros: more flexible.
Cons: bigger diff, more RAM (two extra ring buffers, ~8 kB), and we don't
have a use case for v1.

**Decision: Option A.** Defer mixer to v2 if needed.

---

## 5. Audio task design

A new FreeRTOS task `noise_task`, priority **3** (same as `audio_play`),
stack 4096. Created inside `noise_player_init()` and runs forever.

### Inputs (volatile shared state, written by API, read by task)

```c
static volatile bool          s_play_req;     /* set by play() */
static volatile bool          s_stop_req;     /* set by stop() */
static volatile noise_voice_t s_voice;        /* set anytime */
static volatile noise_voice_t s_voice_target; /* for cross-fade */
static volatile uint8_t       s_vol_target;
static volatile uint16_t      s_timer_target_sec;

static volatile bool          s_playing;
static volatile uint8_t       s_vol_active;   /* what synth multiplies by */
static volatile uint16_t      s_timer_remaining_sec;
```

### Task loop (pseudo-code)

```
noise_task:
  while true:
    if not s_playing and not s_play_req:
      vTaskDelay(50 ms)              # idle, yield CPU
      continue

    if s_play_req:
      audio_player_yield_to_noise()  # stop song, take I2S
      s_play_req = false
      s_playing = true
      s_timer_remaining_sec = s_timer_target_sec
      gain = 0                       # for fade-in
      gain_target = s_vol_target

    # one chunk = 256 samples = 16 ms at 16 kHz
    for i in 0..255:
      sample = synthesize_one(s_voice)
      sample = apply_xfade_if_needed(sample, s_voice, s_voice_target)
      sample = sample * gain
      buf[2i] = buf[2i+1] = sample
    write_stereo(buf, 256)

    # 16 ms passed; update timer counter once per second (every ~62 chunks)
    s_chunk_count++
    if s_chunk_count >= 62:
      s_chunk_count = 0
      if s_timer_remaining_sec != 0xFFFF and s_timer_remaining_sec > 0:
        s_timer_remaining_sec--
        if s_timer_remaining_sec == 0:
          s_stop_req = true   # internal trigger, fade-out path

    # gain ramps
    if gain < gain_target: gain += per-chunk-step
    if gain > gain_target: gain -= per-chunk-step

    if s_stop_req and gain_target != 0:
      gain_target = 0       # start fade-out

    if s_stop_req and gain == 0:
      s_playing = false
      s_stop_req = false
      audio_player_resume_from_noise()
```

### Per-chunk gain step
- Fade-in 3 s = 188 chunks, step = `vol_target / 188`
- Fade-out 12 s = 750 chunks, step = `vol_active / 750`
Computed once when fade starts so the inner loop is a simple add/sub.

### Cross-fade between voices
When `s_voice_target != s_voice`:
- Run *both* synthesisers for ~250 ms (15 chunks).
- Linearly mix `(1-α)·old + α·new`, α going 0→1 over 15 chunks.
- After alpha reaches 1, swap and stop the old generator.

Cost during cross-fade: 2× per-sample synth cost. Voices are < 35 cycles
each, so 2× still well under 1 % CPU.

---

## 6. Synth implementation details (informative — code in v1 patch)

All operations on int16 samples. Voices are pure functions of state held in
file-static variables.

### 6.1 White
```
state: uint32_t xs = 0xC0FFEE;
sample: xorshift32 → top 16 bits (signed)
```

### 6.2 Pink (Voss-McCartney, 7 octaves)
```
state: int16_t rows[7]; int32_t run; uint32_t n;
sample:
  n++
  k = ctz(n)     // RISC-V 'ctz' or __builtin_ctz
  if k < 7:
    new = white_sample()
    run += new - rows[k]
    rows[k] = new
  return run / 7
```

### 6.3 Brown (leaky integrator)
```
state: int32_t y = 0;   // Q15 fixed-point
sample:
  y = (32440 * y + 800 * white_sample()) >> 15
  if y >  20000: y =  20000
  if y < -20000: y = -20000
  return (int16_t)y
```
(α=32440/32768=0.99, β=800/32768=0.024 — chosen so output settles to ~13 k
RMS at full white input, leaving headroom for the timer/fade gain.)

### 6.4 Womb
```
state: float t_mod, t_hb;       // accumulators for LFO + heartbeat
        // plus brown state above
sample:
  s = brown_sample()
  // 4 Hz amplitude shape (squared sin, range ~0.25..1)
  e = sinf(2π · 4 · t_mod)
  e = (0.5 + 0.5*e); e = e*e
  s = s * e
  // heartbeat: every 860 ms, two thumps lub-dub
  if (t_hb in lub_window): s += hb_amp * sin(2π·60·t_hb) * window
  if (t_hb in dub_window): s += hb_amp * sin(2π·60·t_hb) * window
  t_mod += 1/16000; t_hb += 1/16000
  if t_hb > 0.860: t_hb -= 0.860
  return clip(s)
```
Trigonometry: precompute one period of squared-sin into a 4000-entry int16
LUT (4 kHz LFO at 16 kHz → 4 samples/cycle is too few — use 4000 samples for
a full second of LFO at high resolution; index = `(t_mod * 4000) % 4000`).
Saves the per-sample `sinf()` call. Heartbeat sine is 60 Hz × 60 ms = 3.6
cycles, also LUT-based.

### 6.5 Rain (v1.5)
```
sample:
  s = pink_sample()
  hp = s - prev_pink                       // 1-pole HPF
  prev_pink = s
  s = s + hp * 0.3                         // +3 dB shelf
  // sparse drops (~3/sec), 5 ms exponential burst at +12 dB
  if rand() % 5333 == 0:
    drop_amp = 4.0; drop_t = 0
  if drop_t < 80:
    s += drop_amp * white_sample() * exp(-drop_t/20)
    drop_t++
```

### 6.6 Fan (v1.5) — biquad LPF + low peak
A direct-form II biquad with coefficients designed for 1-pole LPF at 1.2
kHz (fs=16k) and a 2nd biquad for resonant peak at 200 Hz. ~14 mul/add/sample.

### 6.7 Ocean (v1.5)
Brown noise multiplied by `(0.6 + 0.4·sin(2π · 0.1 · t))`. Same LUT trick.

---

## 7. UI integration details

### 7.1 Inserting the white-noise row
In `build_song_list()` (`ui.c:98`), after the header is added and before
the song-button loop, create the white-noise row using the same pattern
(an `lv_obj_t` with `LV_OBJ_FLAG_CLICKABLE`). Its event callback calls
`ui_show_noise_view()`.

The existing `s_song_btns[40]` array is unaffected; the noise row is a
separate object stored in `s_noise_row`.

### 7.2 New view structure (in `ui_noise.c`)
```
s_noise_view (lv_obj_t)
├── s_n_back_btn
├── s_n_batt_lbl
├── s_n_halo  (lv_obj_t with rounded border)
│   └── s_n_halo_icon (label)
├── s_n_title       (label "White Noise")
├── s_n_subtitle    (label updated from voice)
├── s_n_voice_row   (flex row, one button per voice)
├── s_n_play_btn
│   └── s_n_play_icon
├── s_n_vol_label
├── s_n_vol_slider
├── s_n_vol_pct
├── s_n_timer_label
└── s_n_timer_step  (◀ value ▶)
```

`ui_noise_update()` is called from `ui_update()` when the noise view is
visible. It reads `noise_player_*` queries and updates labels (timer
countdown, percent, voice subtitle, halo pulse, play/pause icon).

### 7.3 Coordinating with `ui_update()`
The current `ui_update()` switches between song list and now-playing based
on `audio_player_is_playing()`. New logic:

```
ui_update():
  noise_playing = noise_player_is_playing()
  song_playing  = audio_player_is_playing()

  if noise_view_visible:
    ui_noise_update()
    return

  # existing song-list <-> now-playing transitions unchanged
```

### 7.4 Pulsing halo animation
LVGL's `lv_anim_t` ramps a custom property. Set up two anims (scale up,
scale down) on a 2 s loop with `LV_ANIM_REPEAT_INFINITE`. Pause on stop.

Alternative cheaper option: just toggle `bg_opa` between 80 % and 100 %
every UI tick (100 ms). Looks nice enough and avoids extra allocations.

---

## 8. NVS persistence

Use existing default `nvs_flash` partition. Namespace `nursery`. Keys as
described in the UI plan. Implementation:

```c
nvs_handle_t h;
nvs_open("nursery", NVS_READWRITE, &h);
nvs_set_u8(h, "wn_voice", s_voice);
nvs_set_u8(h, "wn_vol",   s_vol_target);
nvs_set_u16(h, "wn_timer", s_timer_target_min);
nvs_commit(h);
nvs_close(h);
```

Save on:
- Back button tap from noise view
- Stop (timer expiry or user pause)

Load in `noise_player_init()`. Missing keys → defaults (Pink, 35, 60).

---

## 9. Build / Kconfig changes

### New menu in `Kconfig.projbuild`

```
menu "White Noise Generator"
    config NOISE_ENABLE
        bool "Enable white noise feature"
        default y

    config NOISE_VOICE_WHITE
        bool "White (broadband)"
        default y
        depends on NOISE_ENABLE

    config NOISE_VOICE_PINK
        bool "Pink (1/f, recommended default)"
        default y
        depends on NOISE_ENABLE

    config NOISE_VOICE_BROWN
        bool "Brown (1/f^2, deep)"
        default y
        depends on NOISE_ENABLE

    config NOISE_VOICE_WOMB
        bool "Womb (brown + heartbeat, for newborns)"
        default y
        depends on NOISE_ENABLE

    config NOISE_VOICE_RAIN
        bool "Rain (v1.5)"
        default n
        depends on NOISE_ENABLE

    config NOISE_VOICE_FAN
        bool "Fan (v1.5)"
        default n
        depends on NOISE_ENABLE

    config NOISE_VOICE_OCEAN
        bool "Ocean (v1.5)"
        default n
        depends on NOISE_ENABLE

    config NOISE_DEFAULT_VOLUME
        int "Default playback volume (0-80)"
        range 0 80
        default 35

    config NOISE_VOLUME_CAP
        int "Maximum allowed volume (safety cap, AAP <50 dB at distance)"
        range 30 100
        default 80
endmenu
```

### CMakeLists.txt
Add `noise_player.c` and `ui_noise.c` to `SRCS`. No new dependencies — uses
the same `driver`, `freertos`, `esp_codec_dev`, `nvs_flash`, `lvgl`,
`amoled_driver` already required.

### sdkconfig.defaults
- Confirm `CONFIG_LV_FONT_MONTSERRAT_14=y` is set (currently 12, 16, 18, 22,
  28, 40 are set — 14 is missing). Adding it costs ~6 KB flash.
- Audit: nothing else to change.

---

## 10. RAM / flash budget

### Flash impact
| Item | Estimated bytes |
|------|-----------------|
| `noise_player.c` code | ~10 KB |
| `ui_noise.c` code | ~6 KB |
| Sin / shape LUTs (2 × 4000 × 2 bytes) | 16 KB (`.rodata`) |
| Heartbeat impulse LUT (~960 × 2 bytes) | ~2 KB |
| Montserrat 14 font (LVGL) | ~6 KB |
| Strings, NVS keys | ~1 KB |
| **Total** | **~41 KB** |

Project currently uses ~1.6 MB of the 3 MB factory partition (estimate from
existing similar projects). 41 KB is comfortably under headroom — no
partition resize needed.

### RAM impact
| Item | Bytes |
|------|-------|
| Per-voice synth state (max ~80 bytes × 7 voices) | ~600 |
| Cross-fade scratch buffer | 0 (reuses existing 256-sample stereo buf) |
| `noise_task` stack | 4096 |
| LVGL noise view objects | ~3 KB (estimate from existing now-playing view ~3 KB) |
| **Total** | **~8 KB** |

Headroom from CLAUDE.md budget: ~168 KB. 8 KB is trivial. **OK.**

### CPU impact (worst case = Womb voice)
- Synth: ~35 cycles/sample × 16 kHz = 560 k cycles/s
- LUT-based ones avoid `sinf()` so closer to 25 cycles
- 0.4 % of 160 MHz core
- LVGL UI: ~5 % typical (pre-existing)
- Audio I2S: ~1 % (pre-existing)

Plenty of room.

---

## 11. Concurrency & ordering

| Resource         | Owner / writer     | Reader            | Sync mechanism |
|------------------|--------------------|-------------------|----------------|
| I2S TX channel   | song task ↔ noise task (mutually exclusive) | — | Yield/resume protocol via `s_yielded_to_noise` flag, fence by 30 ms wait |
| Codec volume     | API → noise task (writes once per chunk) | — | volatile uint8 — atomic on RISC-V |
| `s_play_req` etc.| API thread (LVGL task) | noise task    | `volatile`; reads tolerant of momentary stale value |
| LVGL widgets     | LVGL task only     | LVGL task only    | LVGL is not thread-safe; UI updates must remain inside the LVGL timer callback |
| NVS handle       | LVGL task (UI exit point) | LVGL task | Open/close in same call; never shared |

The single-core ESP32-C6 makes most of this trivial — only one task runs at
a time, and `volatile` reads/writes of byte/word-aligned scalars are
atomic.

---

## 12. Failure modes & mitigations

| Failure | Mitigation |
|---------|-----------|
| Codec returns error mid-write | Log once, retry next chunk; if 5 errors in a row, stop with fade-out and disable amp |
| Timer drift over hours of play | We count chunks (62 chunks/s); drift < 1 % typical I2S clock — acceptable |
| User taps voice rapidly | Each tap re-arms the cross-fade; the engine handles re-entry by always interpolating from current `(1-α)` |
| User exits view during fade-out | View hides; fade-out continues on the noise task. Re-entering the view shows "fading…" (no controls editable) |
| BOOT button screen-off mid-playback | No effect — playback continues, screen is dark, audio task untouched |
| Battery very low | (v1) no behaviour. (v2 stretch) auto fade-out when battery < 15 % to avoid AXP2101 shutdown mid-song |
| Codec yield deadlock | The yield path uses a 100 ms timeout. If the song task doesn't release, the noise task aborts and logs; user will retry |

---

## 13. Testing plan

### Unit-level (manual / scripted)

1. **Synthesis spectrum check** — capture 1 s of PCM via UART (`vTaskList`-style
   logging or write-to-stdout debug shim) for each voice; offline FFT in
   Python, verify slope:
   - White: ±2 dB across 100 Hz – 8 kHz
   - Pink: −3 dB/oct ±1 dB
   - Brown: −6 dB/oct ±1 dB
2. **DC offset** — long-running brown noise should not drift; clamp test.
3. **Click test** — record fade-in / fade-out boundaries; verify no
   discontinuity > 200 LSB.
4. **Timer accuracy** — 60-min timer ends within ±5 s of nominal.

### Integration (on-device)

1. White-noise row visible at top of list, distinct from songs.
2. Tap row → opens new view; tap back → returns to list.
3. Tap a song → noise stops cleanly.
4. Tap noise row while song playing → song stops cleanly.
5. Volume slider drags with no zipper noise.
6. Voice swap mid-play → no click.
7. Timer countdown updates each second.
8. Timer expiry triggers 12 s fade-out, view returns to ▶.
9. Reboot → last voice/volume/timer restored from NVS.
10. Long-press BOOT during noise → child lock active, noise continues, all
    touch on noise view absorbed.
11. Short-press BOOT during noise → screen off, noise continues.
12. AAP-safety verification: at default volume 35 % with the device 30 cm
    from a phone SPL meter, peak should be ≤ 65 dB(A); at 7 ft (200 cm)
    should be ≤ 50 dB(A). Record measurements in commit message.

### Long-soak tests

- 8 hr continuous brown noise: log `esp_get_free_heap_size()` every minute,
  verify no leak. Expected: stable ±64 bytes.
- 8 hr loop of 60-min timer + auto-restart (manual restart each hour):
  verify codec re-init isn't needed and amp toggle works.

---

## 14. Phased rollout

### Phase 1 — engine only (1–2 days)
- `noise_player.h/c` with White / Pink / Brown / Womb
- Hardcoded play/stop API; no UI
- Add a debug hook (e.g. boot-time long press) to start noise — verify on
  hardware

### Phase 2 — UI (1–2 days)
- White-noise row in song list
- Full noise view (back, halo, voice picker, play/pause, volume, timer)
- Wire to `noise_player_*` API
- NVS persistence

### Phase 3 — polish (1 day)
- Fade-in / fade-out
- Cross-fade between voices
- Halo animation
- Subtitle / countdown labels
- Final volume cap audit

### Phase 4 — docs and demo (0.5 day)
- README section
- Update `.claude/skills/nursery-rhyme-music/SKILL.md` if applicable
- Capture demo video at 50 dB for the docs/media folder

### Phase 5 (optional) — flavours (1 day)
- Rain / Fan / Ocean voices
- Toggle in voice picker (scrollable row)

Total: **3–5 days of focused work** to ship v1; another day for v1.5
voices.

---

## 15. Open decisions for the maintainer

These are the pending judgment calls that need a +1 before implementation
starts:

1. **Default voice** — Pink (this plan) or Brown? Pink is "safer" for the
   broadest age range; Brown sounds richer through a small speaker.
2. **Timer presets** — `15/30/60/120/∞` proposed. Some sleep-coaches
   recommend "all night" (∞) as default; others say 60 min. Recommended:
   keep ∞ available but default to 60 min (auto-off is the AAP-aligned
   behaviour).
3. **Volume cap** — 80 % proposed; some parents will want louder for
   newborn shusher. Hard cap of 80 with a documented note that exceeding
   AAP guidance is the parent's responsibility, vs. a Kconfig that allows
   raising the cap to 100 for advanced users (recommended: Kconfig).
4. **Noise vs. song** — exclusive-only (this plan) vs. mixer (v2). Exclusive
   is simpler and matches every commercial sound machine; ship v1 with it.
5. **Where to put `ui_noise.c`** — separate file (this plan) or inline in
   `ui.c`. Separate file is cleaner; both are fine.

---

## 16. Risk summary

| Risk | Severity | Probability | Note |
|------|----------|------------|------|
| Speaker can't reproduce brown noise convincingly | Medium | High | Mitigated: shape voices to NS4150B's response in §6 |
| Voice cross-fade clicks on hardware | Low | Medium | 250 ms linear cross-fade + per-sample gain ramp should handle it |
| I2S yield-back deadlock | Medium | Low | 100 ms timeout + log; user retries |
| User exposes baby to >50 dB by misuse | High (parental) | Medium | Conservative defaults + README + UI cap; ultimately user's call |
| RAM blowout from new view widgets | Low | Low | 8 KB headroom audit confirms OK |
| Flash blowout from font/LUT additions | Low | Low | 41 KB on 3 MB partition is fine |

Net: low overall risk. The feature is additive, the synthesis is cheap,
and the UI is a simple new view that reuses every pattern already in the
codebase.

---

## 17. Quick reference — existing code touchpoints

For the implementer's grep convenience:

- `audio_player.c:46-67` — playback state vars; new noise vars sit alongside
- `audio_player.c:421` — `audio_task` loop; needs idle-silence guard
- `audio_player.c:567` — `audio_player_play()`; the model for an interrupt-
  and-restart play API
- `audio_player.c:555` — `audio_player_amp_enable()`; called from noise stop
- `ui.c:98-166` — `build_song_list()`; insertion site for the noise row
- `ui.c:425-504` — `ui_update()`; route to new view when active
- `main/main.c:137-150` — init order; insert `noise_player_init()` between
  `audio_player_init()` and `audio_player_amp_enable(true)`
- `projects/17_pixelpet/main/audio_output.c:155-244` — already has an
  LFSR-based white-noise generator; a useful reference for chunk-write
  patterns and `play_silence()` / `write_stereo()` shape

End of plan. See `WHITE_NOISE_RESEARCH.md` and `WHITE_NOISE_UI_PLAN.md` for
the audio and UX rationale this plan is built on.
