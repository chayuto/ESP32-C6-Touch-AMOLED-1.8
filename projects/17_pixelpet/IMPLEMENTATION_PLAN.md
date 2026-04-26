# 17_pixelpet — Implementation Plan

> Virtual pet toy on the ESP32-C6-Touch-AMOLED-1.8.
> Pixel-art creature with persistent stats that age in real time across power cycles,
> driven by RTC + NVS. Care for it via touch, motion, and sound.
>
> Date: 2026-04-26

---

## Naming & IP

The genre name "virtual pet" / "digital pet" is not protected. The trademark
**Tamagotchi**® is owned by Bandai — we do not use that name, and we do not
replicate any specific Bandai character designs. Our creature is an original
pixel-art design referred to as a **PixelPet**.

---

## Game Design

### Core Loop

```
hatch egg → baby → child → teen → adult → senior → death (or rebirth)
            ↑                                       ↓
            └────── care quality determines ───────┘
                     stage transitions and lifespan
```

### Pet Stats (0–100, all persisted)

| Stat | Decays | Restored by | Effect when low |
|------|--------|-------------|-----------------|
| **Hunger** | -1/30min | Feeding | Health drops, sad sprite |
| **Happiness** | -1/45min | Play, attention | Refuses food, sulks |
| **Energy** | -1/20min when awake | Sleep | Slower animations, yawns |
| **Cleanliness** | -1 per poop event | Wipe action | Sick chance, flies sprite |
| **Discipline** | -1/2h | Discipline action | Misbehavior cycles |
| **Health** | derived | (auto-recovers when other stats high) | At 0 → death |

### Life Stages (real-time, RTC-driven)

| Stage | Duration | Sprite | Notes |
|-------|----------|--------|-------|
| Egg | 0–30 min | Egg with cracks | Tap to incubate |
| Baby | 30 min – 6 h | Tiny round blob | Frequent care needs |
| Child | 6 – 24 h | Slightly bigger blob with limbs | Mini-games unlock |
| Teen | 24 – 72 h | Stylized sprite | Discipline matters |
| Adult | 72 – 168 h (1 week) | Full sprite, evolves based on care | One of 4 forms |
| Senior | 168 h+ | Calm pose | Stats decay slower |
| Death | when Health = 0 | Angel sprite, 24h memorial | Restart available |

### Adult Forms (branching based on care quality during teen stage)

- **Happy form** — high happiness + discipline → bright, animated
- **Lazy form** — low energy upkeep → drowsy sprite
- **Naughty form** — low discipline → mischievous animations
- **Sick form** — low cleanliness/health → recoverable

---

## Hardware Mapping

| Hardware | Role |
|----------|------|
| **AMOLED 368×448** | Pet sprite, stats UI, true blacks for retro feel |
| **PCF85063 RTC** + backup pads | Real-time aging across power-off |
| **NVS flash** | Pet stats, stage, hatch time, save slot |
| **AXP2101** | Battery monitoring, deep sleep between care events |
| **QMI8658 IMU** | Shake to wake, tilt-controlled mini-games |
| **FT3168 touch** | Primary UI — tap care icons, swipe to clean |
| **BOOT button GPIO 9** | Menu / back / wake |
| **ES8311 speaker** | Chiptune chirps, jingles, alert beeps |
| **ES8311 mic** | Optional: clap detection (energy threshold), WakeNet9s "wake" word |
| **TCA9554 P7** | NS4150B speaker amp enable (only on during sounds, save power) |

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                       main.c                              │
│  NVS → amoled → touch → LVGL → RTC → IMU → audio →      │
│  stat_engine → pet_renderer → ui_screens → lvgl_task      │
└──────────────────┬──────────────────────────────────────┘
                   │
   ┌───────────────┼───────────────┬─────────────┬───────────────┐
   ▼               ▼               ▼             ▼               ▼
┌─────────┐  ┌──────────┐  ┌────────────┐  ┌──────────┐  ┌──────────────┐
│pet_state│  │stat_engine│  │pet_renderer│  │ui_screens│  │minigames     │
│         │  │           │  │            │  │          │  │              │
│NVS save │  │decay tick │  │sprite anim │  │status    │  │catch_apple   │
│NVS load │  │stage xform│  │frame loop  │  │feed      │  │balance       │
│RTC sync │  │care apply │  │mood→pose   │  │play      │  │rhythm        │
│         │  │death chk  │  │            │  │clean     │  │              │
│         │  │           │  │            │  │sleep     │  │              │
└─────────┘  └──────────┘  └────────────┘  └──────────┘  └──────────────┘
                   │                              │
                   ▼                              ▼
            ┌──────────────┐              ┌──────────────┐
            │audio_jingles │              │input_manager │
            │ chiptune gen │              │ btn/touch/imu│
            │ event sounds │              │ event queue  │
            └──────────────┘              └──────────────┘
```

### Data Flow

```
RTC tick (every 60s, low-power timer)
  → read PCF85063
  → compute Δt since last_update_unix in NVS
  → stat_engine.apply_decay(Δt)
  → check stage transition / death
  → write back to NVS

LVGL animation timer (50 ms / 20 Hz)
  → pet_renderer.tick() — sprite frame, idle animation
  → ui_screens.refresh_stats() — bar widgets

LVGL input timer (100 ms / 10 Hz)
  → input_manager.poll() — debounce button, IMU shake event
  → dispatch to active screen

Care action (touch event)
  → screen handler computes stat delta
  → stat_engine.apply_care(action_t)
  → audio_jingles.play(JINGLE_HAPPY)
  → pet_renderer trigger reaction animation
  → schedule NVS commit (debounced 5s)
```

---

## File Structure

```
projects/17_pixelpet/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── sdkconfig.defaults.template
├── IMPLEMENTATION_PLAN.md
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml
    ├── Kconfig.projbuild        — debug toggles (fast aging, skip egg)
    ├── main.c                   — App init, LVGL task, FSM bootstrap
    ├── pet_state.c / .h         — Stats struct, NVS save/load, RTC sync
    ├── stat_engine.c / .h       — Decay, care application, stage transitions
    ├── pet_renderer.c / .h      — Sprite animation, mood→pose mapping
    ├── ui_screens.c / .h        — Screen FSM, stat bars, menu
    ├── audio_jingles.c / .h     — Chiptune generator, jingle library
    ├── input_manager.c / .h     — Button, touch, IMU event aggregation
    ├── rtc_manager.c / .h       — PCF85063 wrapper
    ├── power_manager.c / .h     — AXP2101 sleep/wake, brightness
    ├── minigame_catch.c / .h    — Tilt-controlled catching game
    ├── minigame_balance.c / .h  — Tilt balance challenge
    ├── minigame_rhythm.c / .h   — Shake-to-rhythm game
    └── sprites.h                — Embedded pixel-art bitmap arrays
```

---

## Hardware Unlocked (First Time in This Repo)

| Peripheral | Address/Pin | Purpose |
|-----------|-------------|---------|
| **PCF85063 RTC** | I2C 0x51 | Wall-clock time for offline aging |
| **AXP2101 sleep** | I2C 0x34 | Deep sleep between care events |
| **NVS namespace `pixelpet`** | flash | Persistent pet save state |

Reuses from earlier projects:
- AMOLED + LVGL (project 11+)
- FT3168 touch (project 11+)
- QMI8658 IMU (project 14)
- ES8311 audio out + speaker amp (project 14, 15)
- BOOT button (project 12+)

---

## Persistence Model (the killer feature)

### Save Slot Layout (NVS namespace `pixelpet`)

| Key | Type | Description |
|-----|------|-------------|
| `version` | u8 | Save schema version (start at 1) |
| `hatched_unix` | i64 | Unix time of egg hatch |
| `last_update_unix` | i64 | Last stat decay applied at this RTC time |
| `stage` | u8 | enum: egg, baby, child, teen, adult, senior, dead |
| `adult_form` | u8 | enum: happy, lazy, naughty, sick (set at adult transition) |
| `hunger` `happy` `energy` `clean` `disc` `health` | u8 each | Stats 0–100 |
| `care_score` | u32 | Cumulative care quality, used to pick adult form |
| `poop_count` | u8 | Active poops on screen |
| `is_sleeping` | u8 | Boolean |

### Boot Sequence (the magic)

```
1. NVS load → pet_state struct
2. RTC read → now_unix
3. Δt = now_unix - last_update_unix
4. stat_engine.fast_forward(Δt)       — apply all missed ticks
5. Check transitions / death          — pet may have aged into a new stage
                                        or died while powered off
6. If died: show memorial screen
7. last_update_unix = now_unix
8. Schedule NVS commit
```

This means leaving the device off for 8 hours and turning it back on shows a
genuinely older, hungrier pet — exactly like the original Bandai toy. The RTC
backup battery (board has pads, user installs CR1220) keeps time even with
main battery removed.

### NVS Commit Strategy

- Debounced 5s after any stat change → coalesces rapid touch events
- Forced commit before deep sleep
- Forced commit on stage transition or death
- Avoid committing on every animation frame (flash wear)

---

## Power Management

### Active States

| State | Display | CPU | Wake-up sources |
|-------|---------|-----|-----------------|
| **Active** | On, full brightness | 160 MHz | — |
| **Dim** | On, brightness 30 | 160 MHz | Activity → Active |
| **Screen off** | Off | 80 MHz | Touch INT, button, shake |
| **Light sleep** | Off | sleep | RTC alarm (60s), GPIO INT |
| **Deep sleep** | Off | off | RTC alarm (15min), GPIO INT |

### Transitions

```
Active ── 30s no input ──→ Dim
Dim    ── 60s no input ──→ Screen off
Screen off ─ 5min no input → Light sleep (1min ticks)
Light sleep ─ pet sleeping → Deep sleep (15min ticks)
Any wake source → Active (with stat fast-forward on resume)
```

### Wake Sources

- **GPIO 9 BOOT button** — RTC GPIO ext0
- **GPIO 15 Touch INT** — RTC GPIO ext1
- **IMU shake** — QMI8658 INT pin → wake
- **PCF85063 alarm** — periodic wake for stat tick
- **AXP2101 PWR button** — power on from full off

---

## Input Mapping

### Touch (primary)

| Action | Effect |
|--------|--------|
| Tap stat icon | Show stat detail / care submenu |
| Tap food icon (feed screen) | Feed pet |
| Tap soap icon (clean screen) | Wipe poop |
| Swipe across poop | Wipe poop |
| Tap pet sprite | Pet reacts (happiness +1) |
| Long-press pet | Pick up animation |
| Swipe edge L/R | Cycle screens |
| Swipe up | Show menu |

### BOOT Button (GPIO 9)

| Press | Effect |
|-------|--------|
| Short | Cycle screen forward |
| Long (1s) | Back to status |
| Double | Toggle sleep |

### IMU (QMI8658)

| Gesture | Effect |
|---------|--------|
| Shake | Wake from sleep, "discipline" if pet is misbehaving |
| Tilt | Mini-game control |
| Tap (sharp accel spike) | Notify pet (mild attention) |

### Mic (ES8311) — optional, future

| Sound | Effect |
|-------|--------|
| Loud clap | Pet looks at you |
| WakeNet9s "Hi ESP" | Pet greets back |

---

## Sprite & Animation System

### Sprite Format

- **Frame size:** 64×64 px, RGB565, indexed against 16-color palette to halve flash usage
- **Sheet layout:** N frames horizontal, stored as `const uint8_t sprite_xxx[]` in `sprites.h`
- **Palette:** `static const lv_color_t palette[16]` — retro 16-color
- **Storage:** All sprites in flash (`const`), never copied to RAM

### Frame Counts (per stage)

| Stage | Idle frames | Eat frames | Sleep frames | Sad frames | Total bytes |
|-------|-------------|------------|--------------|------------|-------------|
| Egg | 4 | — | — | — | 8 KB |
| Baby | 4 | 4 | 2 | 2 | 24 KB |
| Child | 6 | 4 | 2 | 2 | 28 KB |
| Teen | 6 | 4 | 2 | 4 | 32 KB |
| Adult ×4 forms | 6 each | 4 each | 2 each | 4 each | 128 KB |
| Senior | 6 | 4 | 2 | 4 | 32 KB |
| Effects (poop, sparkle, food) | — | — | — | — | 8 KB |
| **Total sprites in flash** | | | | | **~260 KB** |

Comfortable in a 3 MB app partition.

### Mood → Pose Mapping

```c
typedef enum { MOOD_HAPPY, MOOD_NEUTRAL, MOOD_SAD, MOOD_HUNGRY,
               MOOD_TIRED, MOOD_SICK, MOOD_PLAYING } mood_t;

mood_t derive_mood(const pet_state_t *p) {
    if (p->health < 30 || p->clean < 20) return MOOD_SICK;
    if (p->hunger < 25) return MOOD_HUNGRY;
    if (p->energy < 20) return MOOD_TIRED;
    if (p->happy < 30) return MOOD_SAD;
    if (p->happy > 80 && p->hunger > 60) return MOOD_HAPPY;
    return MOOD_NEUTRAL;
}
```

### Render Loop

LVGL `lv_image` widget, `lv_image_set_src()` cycles through frames at 4–8 Hz
depending on mood. Use a single fullscreen `lv_canvas` or sprite layer + UI
overlay (stat bars, icons).

---

## Audio Jingles

Built on `audio_output` style from project 14 (16 kHz, 16-bit, ES8311 DAC,
NS4150B amp via TCA9554 P7).

### Synthesis

- **Square wave + envelope** (NES/Game Boy style)
- **2-channel polyphony** (melody + bass)
- **Note table** — 12-tone equal temperament, A4 = 440 Hz

### Jingle Library

| Event | Length | Description |
|-------|--------|-------------|
| `JINGLE_HATCH` | 1.5 s | Cheerful 4-note rise |
| `JINGLE_EAT` | 0.5 s | 3-note munch |
| `JINGLE_HAPPY` | 0.8 s | Major arpeggio up |
| `JINGLE_SAD` | 0.8 s | Minor 3rd descend |
| `JINGLE_POOP` | 0.3 s | Wet plop (noise burst) |
| `JINGLE_SICK` | 1.0 s | Wobbling pitch bend |
| `JINGLE_DEATH` | 3.0 s | Slow descending minor |
| `JINGLE_LEVELUP` | 2.0 s | Fanfare on stage transition |
| `JINGLE_ALARM` | repeat | Beep-beep when stat critical |

Total flash for jingles: ~0 KB (synthesized, not PCM).

### Amp Power

- Enable amp 50 ms before jingle starts (avoid pop)
- Disable amp 200 ms after jingle ends
- Saves ~20 mA when idle

---

## UI Screens

### Screen FSM

```
   ┌──────────────┐
   │   STATUS     │ ←─ default
   │ pet + bars   │
   └──────┬───────┘
          │ swipe / button
   ┌──────┴───────────┬───────────┬────────────┬────────────┐
   ▼                  ▼           ▼            ▼            ▼
┌──────┐ ┌──────────┐ ┌─────────┐ ┌──────────┐ ┌──────────┐
│ FEED │ │   PLAY   │ │  CLEAN  │ │  SLEEP   │ │  STATS   │
│ menu │ │ minigame │ │  swipe  │ │ lights   │ │ detail   │
└──────┘ └──────────┘ └─────────┘ └──────────┘ └──────────┘
```

### Status Screen (default)

```
┌─────────────────────────┐
│  AGE: 2d 4h    ❤️ 78    │
│ ─────────────────────── │
│                         │
│        [pet sprite]     │
│         (animated)      │
│                         │
│       💩    💩          │  poops if any
│ ─────────────────────── │
│ 🍔 ░░░░░░██  HUNGER 78  │
│ 😊 ░░░██████ HAPPY  62  │
│ ⚡ ░░░░████  ENERGY 45  │
└─────────────────────────┘
```

### Layout Constraints (368×448)

- Top bar: 40 px (age, health icon)
- Pet area: 280 px tall (centered sprite + effects)
- Bottom stat bars: 128 px (3 bars, 40 px each)

---

## Stat Engine

### Decay Function

```c
void stat_engine_decay(pet_state_t *p, int64_t dt_seconds) {
    // Decay rates per second (precomputed at compile time)
    p->hunger = clamp_decay(p->hunger, dt_seconds, HUNGER_DECAY_PER_SEC);
    p->happy  = clamp_decay(p->happy,  dt_seconds, HAPPY_DECAY_PER_SEC);
    if (!p->is_sleeping)
        p->energy = clamp_decay(p->energy, dt_seconds, ENERGY_DECAY_PER_SEC);
    else
        p->energy = clamp_grow (p->energy, dt_seconds, ENERGY_GROW_PER_SEC);

    // Health derived: drops when other stats critical
    int critical = (p->hunger < 20) + (p->happy < 20) + (p->clean < 20);
    if (critical >= 2)
        p->health = clamp_decay(p->health, dt_seconds, HEALTH_DECAY_PER_SEC);
    else if (p->hunger > 60 && p->happy > 60 && p->clean > 60)
        p->health = clamp_grow(p->health, dt_seconds, HEALTH_GROW_PER_SEC);

    // Random poop event
    if (random_chance_per_sec(POOP_RATE) && p->poop_count < 3)
        p->poop_count++;

    // Death check
    if (p->health == 0) p->stage = STAGE_DEAD;
}
```

### Care Actions

```c
typedef enum {
    CARE_FEED_MEAL,    // hunger +30, happy +5
    CARE_FEED_SNACK,   // hunger +10, happy +15, weight +1
    CARE_PLAY,         // happy +20, energy -15
    CARE_CLEAN,        // clean +50 per wipe
    CARE_SLEEP_TOGGLE, // is_sleeping flips
    CARE_DISCIPLINE,   // disc +20, happy -5
    CARE_MEDICINE,     // health +30 (only when sick)
} care_action_t;

void stat_engine_apply_care(pet_state_t *p, care_action_t action);
```

### Stage Transitions

```c
static const struct { stage_t stage; uint32_t hours_into_life; } stage_table[] = {
    { STAGE_EGG,    0    },
    { STAGE_BABY,   0    },  // immediately on hatch
    { STAGE_CHILD,  6    },
    { STAGE_TEEN,   24   },
    { STAGE_ADULT,  72   },
    { STAGE_SENIOR, 168  },
};
```

Adult form chosen at the teen→adult transition based on `care_score`
(cumulative care quality during teen stage).

---

## Mini-games

### 1. Catch the Apple (Child+)

- Pet at bottom, apples fall from top
- Tilt device to move pet left/right
- Catch increases hunger; miss does nothing
- 30 s round, score = apples caught
- Score → happiness boost

### 2. Balance (Teen+)

- Pet stands on platform that wobbles based on tilt
- Keep tilt within deadzone for as long as possible
- Time → happiness + energy efficiency boost

### 3. Rhythm Shake (Adult+)

- 4-beat pattern shown on screen
- Shake device on each beat
- IMU shake events compared to expected timing
- Combo → happiness + bonus item unlock

---

## Dependencies

### idf_component.yml

```yaml
version: "1.0.0"
dependencies:
  espressif/esp_codec_dev: "^1.4.0"   # ES8311 codec
  waveshare/qmi8658: "*"               # IMU
  idf: ">=5.5.0"
```

### Shared Component

- `amoled_driver` via `EXTRA_COMPONENT_DIRS` (display, touch, PMIC, I2C bus, IO expander)

### PCF85063 RTC

ESP-IDF has no first-party PCF85063 driver in the registry. Two options:

1. **Inline driver** — write thin wrapper in `rtc_manager.c` (~150 lines: read/write
   BCD time registers over I2C). PCF85063 is well-documented, simple chip.
2. **Use `c2h2/pcf85063` or similar community component** — investigate registry first.

**Decision:** start with inline driver to avoid dependency lock-in; promote to a
shared component if a future project needs RTC.

---

## Init Sequence

```
1.  NVS flash init                 — pet save slot
2.  GPIO 9 BOOT button init
3.  amoled_init()                  — I2C → PMIC → TCA9554 → QSPI → SH8601
4.  amoled_touch_init()            — FT3168
5.  amoled_lvgl_init()             — LVGL display + touch input
6.  rtc_manager_init()             — PCF85063 on shared I2C (0x51)
7.  imu_manager_init()             — QMI8658 (0x6B) + INT pin
8.  audio_output_init()            — I2S TX + ES8311 DAC
9.  Speaker amp gpio setup         — TCA9554 P7 = LOW (off until needed)
10. pet_state_load()               — NVS read, struct populate
11. rtc_sync_and_fast_forward()    — apply Δt decay since last_update_unix
12. ui_screens_init()              — create LVGL screens, route to status
13. pet_renderer_init()            — load initial sprite for current stage/mood
14. Start LVGL task                — animation 50ms, input poll 100ms
15. Start RTC tick task            — 60s wake, decay, NVS debounce commit
16. amoled_set_brightness(150)
```

No WiFi. No Bluetooth. Self-contained toy.

---

## RAM Budget

```
LVGL draw buffers (×2):     66 KB   (368×45×2 × 2)
LVGL heap:                  48 KB   (CONFIG_LV_MEM_SIZE_KILOBYTES=48)
Pet state struct:            1 KB
Sprite working buffer:       8 KB   (one frame decompressed if needed)
I2S TX DMA:                  4 KB
Synth state:                 1 KB
IMU state + task:            5 KB
RTC tick task:               4 KB
LVGL task:                   8 KB
Main task:                   4 KB
Mini-game state:             2 KB
NVS overhead:                4 KB
Misc / heap:                10 KB
─────────────────────────────────────
Total:                    ~165 KB
Free of 512 KB:           ~290 KB   (very comfortable, no WiFi)
```

---

## Build Configuration

### sdkconfig.defaults

```
CONFIG_IDF_TARGET="esp32c6"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_COMPILER_OPTIMIZATION_PERF=y

# LVGL
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_28=y
CONFIG_LV_MEM_SIZE_KILOBYTES=48
CONFIG_LV_COLOR_16_SWAP=y

# FreeRTOS
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_UNICORE=y

# No Bluetooth (save RAM)
CONFIG_BT_ENABLED=n

# NVS
CONFIG_NVS_ENCRYPTION=n

# Sleep / power
CONFIG_PM_ENABLE=y
CONFIG_PM_DFS_INIT_AUTO=y
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y

# Watchdog
CONFIG_ESP_TASK_WDT_TIMEOUT_S=15
```

### partitions.csv

```
# Name,    Type, SubType, Offset,   Size,   Flags
nvs,       data, nvs,     ,         0x6000,
phy_init,  data, phy,     ,         0x1000,
factory,   app,  factory, ,         3M,
```

3 MB app fits ~260 KB sprites + LVGL + drivers comfortably.

---

## Implementation Phases

### Phase 1 — Foundation (no save persistence)

1. Scaffold project (CMake, sdkconfig.defaults, partitions.csv, idf_component.yml)
2. Bring up display + touch + LVGL "Hello Pet" placeholder
3. Add BOOT button + screen cycle FSM (status/feed/play/clean/sleep stubs)
4. Verify on hardware

### Phase 2 — Pet State + Renderer

1. Implement `pet_state_t` struct + in-memory only
2. Hand-draw 1 baby + 1 child idle sprite (4 frames each), encode to `sprites.h`
3. Renderer cycles frames, mood→pose mapping
4. Stat bars on status screen
5. Synthetic time tick (1 game-hour per real-second toggle for debug)

### Phase 3 — Stat Engine + Care Actions

1. Decay function with proper time delta
2. Feed / play / clean handlers wired to touch
3. Stage transition table; visible transition animation
4. Audio jingles for care events

### Phase 4 — Persistence (the killer feature)

1. NVS save/load with debounced commit
2. PCF85063 RTC integration
3. Boot-time fast-forward of Δt
4. Manual test: flash, run, power-off 1h, power-on → see hunger drop

### Phase 5 — Mini-games + IMU

1. QMI8658 init + tilt + shake events
2. Catch-the-apple game
3. Wire IMU to play screen

### Phase 6 — Power Management

1. AXP2101 brightness ramp on idle
2. Light sleep with RTC alarm + GPIO wake
3. Deep sleep when pet sleeps long-term
4. Verify aging still works across deep sleep

### Phase 7 — Polish

1. All adult forms drawn
2. Death + memorial flow
3. All jingles tuned
4. Senior stage decay rate balance
5. Hardware battery test for 24h pet life

Each phase ends with a hardware build/flash/sanity check.

---

## Build Steps

```bash
. ~/esp/esp-idf/export.sh
cd projects/17_pixelpet
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/cu.usbmodem3101 flash monitor
```

---

## Open Questions

- **RTC backup battery** — board has CR1220 pads but no cell shipped. Need to
  source one to truly preserve pet across full power-off (battery + USB removed).
  Without it, pet lives only across software resets, not unplug events.
- **Sprite art source** — hand-drawn pixel art vs. AI-generated and curated?
  Either works; should pick a consistent palette early so all sprites match.
- **Save slot count** — single pet or 3 saves? Single is simpler; multi adds
  family-friendliness. Defer to Phase 7.
- **Network features** — none planned. If we add WiFi later for OTA or for a
  "playdate" feature with another PixelPet, that's a separate project.
