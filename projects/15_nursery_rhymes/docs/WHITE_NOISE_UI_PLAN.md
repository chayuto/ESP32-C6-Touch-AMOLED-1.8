# White Noise — UI / UX Plan

UI design for the white-noise generator added to the Nursery Rhyme Player.
Targets the existing 368 × 448 portrait AMOLED with FT3168 capacitive
touch.

This document describes screens, navigation, layout, sizes, colours, and
interaction states. It does **not** include code.

---

## 1. Information architecture

Three views total; only one is visible at a time. The existing two views
(Song List, Now Playing) are unchanged in behaviour. A new third view is
added.

```
                ┌──────────────────┐
                │   Song List      │ ◀── boot view, scrollable
                │ (40 songs +      │
                │  white-noise hdr)│
                └──────────────────┘
                  │             │
            tap song          tap white-noise row
                  │             │
                  ▼             ▼
        ┌──────────────┐  ┌──────────────────┐
        │ Now Playing  │  │  White Noise     │
        │  (existing)  │  │  (new)           │
        └──────────────┘  └──────────────────┘
                  │             │
              [ back ]      [ back ]
                  │             │
                  ▼             ▼
                Song List   Song List
```

The two playback views are mutually exclusive — selecting a song while
noise is playing stops the noise (and vice versa). This matches the
existing `audio_player_play()` semantics, which already aborts whatever is
playing.

The white-noise view does **not** auto-close when a timer expires; the user
sees the timer reach 0 and the playback stops with fade-out. The view stays
on screen until "Back" is tapped.

---

## 2. Song-list view — adding the entry point

### Current layout
- 52 px header row at the top: "🎵 Nursery Rhymes" label.
- 40 song rows below, 56 px tall each, 4 px gap, full screen width.
- Header is a static obj inside the scrollable container. It scrolls with
  the list.

### New layout
Insert a new **"White Noise" row** as the first item in the scrollable
list, *below the header*. Visually distinct from songs so it doesn't get
confused with a track.

```
┌──────────────────────────────────────────────┐  ◀── 52 px
│ 🎵  Nursery Rhymes                            │      header (existing)
├──────────────────────────────────────────────┤
│                                              │
│   🌙   White Noise                       ❯  │  ◀── 64 px (taller)
│        Continuous sleep sounds               │      new entry
│                                              │
├──────────────────────────────────────────────┤  ◀── extra 8 px gap
│  1    Twinkle Twinkle Little Star        ▶  │      first song
│       Twinkle, twinkle, little star          │  ◀── 56 px
├──────────────────────────────────────────────┤
│  2    Mary Had a Little Lamb             ▶  │
│       Mary had a little lamb…                │
├──────────────────────────────────────────────┤
│  …                                           │
```

### Styling differences vs. song rows

| Property        | Song row                     | White-noise row                |
|-----------------|------------------------------|--------------------------------|
| Height          | 56 px                        | **64 px** (4 px more padding) |
| Background      | `COL_ITEM_BG` (25,25,55)     | `COL_NOISE_BG` (40,30,70)     |
| Border          | none                         | 1 px bottom, `COL_ACCENT`     |
| Left icon       | 14 px number, accent color   | 28 px moon glyph 🌙 (LV_SYMBOL_NEW_LINE or custom), `COL_NOTE` |
| Title           | song title, 16 pt white      | **"White Noise"**, 18 pt white |
| Subtitle        | first lyric line, 12 pt dim  | **"Continuous sleep sounds"**, 12 pt dim |
| Right icon      | ▶ play (LV_SYMBOL_PLAY)     | ❯ chevron (LV_SYMBOL_RIGHT)   |
| Pressed bg      | `COL_ITEM_PRESSED`           | slightly lighter than `COL_NOISE_BG` |
| Tap behaviour   | start playing that song      | open White-Noise view         |

A small visual gap (8 px) is added between the white-noise row and the
first song row to reinforce that it's a different category, and the bottom
border on the white-noise row mirrors a section divider.

### Why "first row" and not a separate tab/page

- The user already has a scrollable list as their primary view; adding a
  tab bar costs vertical space and complicates the touch UX.
- Sleep-sound is a single feature, not a category; one row at the top is
  enough discoverability.
- 1-tap path from home to the white-noise view is the goal — a tab bar
  would still take 1 tap; this saves screen real estate.

### Localisation hooks
The literal strings "White Noise" and "Continuous sleep sounds" live in one
place (a `static const char *` in `ui.c`). Easy to swap later.

---

## 3. White Noise view — full-screen layout

Total screen: 368 × 448. Origin top-left. Coordinate budget below sums to
< 448 px vertical so nothing clips.

```
┌──────────────────────────────────────────────┐  y=0
│  ◀                              🔋 100% ⚡  │  y=8..48   header strip
├──────────────────────────────────────────────┤
│                                              │
│             ╭─────────────╮                  │
│            (    🌙        )                  │  y=70..170 big icon
│             ╰─────────────╯                  │            (animated halo)
│                                              │
│                White Noise                   │  y=180     title
│                Pink — softest                │  y=210     voice subtitle
│                                              │
│  ◀  ◀ White   Pink ●  Brown  Womb  ▶  ▶     │  y=240..285 voice picker
│  ──────────────────────────────────────────  │           (horizontal scroll)
│                                              │
│              ▶  PLAY                         │  y=300     primary action
│           (90 px round)                      │
│                                              │
│  Volume                                      │  y=360
│  ──────────●──────────  35 %                 │  y=380     volume slider
│                                              │
│  Timer  60 min                  ◀  ▶         │  y=410     timer stepper
└──────────────────────────────────────────────┘  y=448
```

### Region-by-region detail

#### 3.1 Header strip (y=0..50)
- Back button — 50×40 hit area, top-left, `LV_SYMBOL_LEFT`. Tapping returns
  to the song list. Does **not** stop playback — noise continues in the
  background, same way songs do today when you back out. Documented behaviour.
- Battery gauge — top-right, mirrors the existing now-playing battery
  widget. Reuses the same 2-second poll on `amoled_get_battery_info()`.

#### 3.2 Halo / icon (y=70..170)
- Centered circle, radius 50 px, `COL_ACCENT` border, transparent interior.
- A 🌙 (or `LV_SYMBOL_AUDIO`) glyph at 40 pt centered in the circle, color
  `COL_NOTE`.
- When playing: subtle pulsing animation (0.95–1.05 scale, 2 s sinewave) by
  toggling LVGL style transforms. The pulse rate matches the typical
  resting heart rate (~60 BPM) for a calming visual cue.
- When paused: static, dimmed (`COL_TEXT_DIM`).

#### 3.3 Title and voice subtitle (y=180..230)
- Title "White Noise" — 22 pt, white, centered. Static.
- Subtitle — 14 pt, dim, centered. Updates with selected voice:
  - White → "White — broadband"
  - Pink → "Pink — softest"
  - Brown → "Brown — deep"
  - Womb → "Womb — heartbeat"
  - (rain/fan/ocean if v1.5 enables them)

#### 3.4 Voice picker (y=240..290)
Horizontal segmented strip with the four voices. Implementation: an
`lv_obj_t` with row flex and `LV_FLEX_ALIGN_CENTER`, each child a 70×40 px
button. Selected button is filled with `COL_ACCENT`; others have
transparent background and 1 px border.

```
[ White ] [Pink ●] [ Brown ] [ Womb ]
```

If we add three more voices, the row scrolls horizontally and the chevrons
(◀ ▶) shown above act as arrow indicators (the row itself is swipeable —
the chevrons are decorative).

Tap → switches voice. While playing, the active synth is changed
mid-stream with a 250 ms cross-fade (50 ms hard transition would click).

#### 3.5 Play / Pause button (y=300..390)
- 90 × 90 px round button, centered horizontally.
- Background: `COL_ACCENT` when paused (call to action), `COL_STOP_BTN`
  when playing.
- Icon: `LV_SYMBOL_PLAY` when paused, `LV_SYMBOL_PAUSE` when playing
  (28 pt, white).
- Tap toggles. Holding (≥ 800 ms) does **not** do anything special in v1
  (reserved for a future "loud-noise burst" Karp-style mode).
- This is the largest touch target on the page — toddler-resistant: easy for
  parent, hard to hit incidentally with a face/cheek.

#### 3.6 Volume slider (y=360..400)
- "Volume" label, 12 pt dim, left-aligned.
- LVGL `lv_slider`, full width minus 40 px margins.
- Range 0–80, default 35. Cap at 80 (not 100) — see safety section in the
  research doc.
- Thumb is large (24 px) for easy touch.
- Numeric percentage shown to the right, 14 pt.
- Drag is debounced — codec volume only updated when the user releases, but
  internal gain ramps smoothly. Avoids zipper noise mid-drag.

#### 3.7 Timer stepper (y=410..445)
- "Timer" label, 14 pt, left.
- Current value shown at center: `15 min`, `30 min`, `60 min`, `2 hr`, `∞`.
- Two chevron buttons (◀ ▶) on the right cycle through the presets.
- When ∞ is selected the value shows the unicode infinity symbol.
- When playing, the value counts down: e.g. `42:18` (mm:ss) instead of
  `60 min`. Format flips back to preset when stopped.

When the timer hits zero:
1. 12 s linear fade-out (volume ramped to 0 in synthesis).
2. Stop synthesis, disable amp.
3. UI: play button reverts to ▶, timer label resets to its preset value
   (so the user sees what it'll start with next time), halo animation stops.
4. View stays open. Optional: dim the screen (re-using `ui_set_screen_off`)
   after a configurable idle window — this is a stretch goal for v2.

---

## 4. Interaction states

### 4.1 Stopped state (default on view enter)
- Play button: ▶, accent color
- Halo: static, dim
- Voice picker: shows last-used voice (NVS-restored)
- Volume: shows last-used value
- Timer: shows last-used preset
- Subtitle: shows voice description

### 4.2 Playing state
- Play button: ⏸, stop color
- Halo: pulsing
- Voice picker: tap re-cross-fades, no stop
- Volume: live drag updates synth gain
- Timer: counts down

### 4.3 Fading-out state (timer end or "Pause" tapped)
- Play button: ⏸ greyed out, untouchable for ~12 s
- Halo: pulsing slows visually as gain drops
- Voice picker: locked
- Volume: locked
- Timer: paused at 0:00 / current value
- Tapping back is allowed and immediately cuts the fade (jumps to silence
  + fade)

### 4.4 Child-lock state
The existing `s_child_locked` flag in `ui.c` (long-press BOOT) blocks
touches. The white-noise view must respect it the same way the song list
and now-playing views do — early-return in event callbacks if locked.
Reuse the existing lock overlay; no changes needed.

---

## 5. Visual style

Reuses the palette from `ui.c`:

```
COL_BG            (10, 10, 30)    page background
COL_HEADER_BG     (30, 20, 60)    header strip
COL_ITEM_BG       (25, 25, 55)    inactive button
COL_ITEM_PRESSED  (60, 40, 100)   pressed
COL_ACCENT        (120, 80, 220)  primary action / selected
COL_NOTE          (255, 200, 80)  amber accent (icons)
COL_STOP_BTN      (200, 60, 60)   playing / stop
COL_TEXT          white
COL_TEXT_DIM      (160, 160, 180) secondary text
```

New: `COL_NOISE_BG (40, 30, 70)` — used only for the entry row in the song
list and as the subtle gradient backdrop of the white-noise view.

The white-noise view itself uses `COL_BG` as the base, so it feels like a
direct child of the song list rather than a separate app.

---

## 6. Touch target audit

All touch targets are at least 44 × 44 px (Apple HIG minimum) so even with
imprecise toddler taps they remain reliable:

| Target              | Size      | Notes                          |
|---------------------|-----------|--------------------------------|
| Back button         | 50 × 40   | top-left, generous slop        |
| Voice picker chip   | 70 × 40   | 4 of them                      |
| Play / pause        | 90 × 90   | the primary action             |
| Volume slider thumb | 24 × 24 hit on a 24 px track | LVGL hit area can be enlarged with `lv_obj_set_ext_click_area` |
| Timer +/-           | 44 × 40   | pair, right side               |

---

## 7. Visual mock — the white-noise row in context

ASCII compressed render showing the song list with the new entry:

```
┌──────────────────────────────────────────┐
│ 🎵  Nursery Rhymes                        │  header (52 px, scrolls)
├──────────────────────────────────────────┤
│ 🌙  White Noise                       ❯  │  64 px, accented
│     Continuous sleep sounds              │
├──────────────────────────────────────────┤   ↑ visual divider
│                                          │   ↓ small extra gap
│  1   Twinkle Twinkle Little Star      ▶  │  56 px
│      Twinkle, twinkle, little star       │
├──────────────────────────────────────────┤
│  2   Mary Had a Little Lamb           ▶  │
│      Mary had a little lamb…             │
├──────────────────────────────────────────┤
│  3   Baa Baa Black Sheep              ▶  │
│      Baa baa black sheep…                │
└──────────────────────────────────────────┘
```

---

## 8. Behaviour matrix vs. existing playback

What happens to noise when the user does X:

| Action                                        | Effect on noise                  |
|-----------------------------------------------|----------------------------------|
| Tap song from list                            | Noise fade-out 1 s, then song    |
| Tap white-noise row from list while song plays| Song stops cleanly, view opens   |
| Press BOOT (short)                            | Screen off; noise keeps playing  |
| Long-press BOOT                               | Child lock; noise keeps playing  |
| Power button (AXP2101)                        | Reboot; noise stops              |
| Battery low                                   | No special behaviour (v1)        |
| Plug/unplug USB charge                        | No effect on playback            |

---

## 9. Persistence

NVS keys (in a new `nursery` NVS namespace if not existing, else fall back
to default `nvs`):

| Key        | Type   | Range          | Default | Description                |
|------------|--------|----------------|---------|----------------------------|
| `wn_voice` | uint8  | 0..6           | 1 (Pink)| Last selected voice index  |
| `wn_vol`   | uint8  | 0..80          | 35      | Last volume                |
| `wn_timer` | uint16 | 0,15,30,60,120,0xFFFF (∞) | 60 | Last timer preset (min) |

Persisted on view exit (back button) and on stop. Read on view init.

---

## 10. Out-of-scope for v1

Things explicitly *not* in this design but easy to add later:

- Multiple custom presets ("Newborn", "Toddler", "Travel")
- Mixing two voices (pink + heartbeat at independent gains)
- Schedule-based playback ("auto-start at 8 PM")
- Equaliser / parametric tuning
- Bedtime story narration
- Linking timer to a fade-to-amber screen colour for circadian dimming

These are nice ideas. None of them block shipping the minimal four-voice
sleep player, and adding them later doesn't require throwing away the v1
data model or UI structure.
