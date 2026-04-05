# 14_sensory_play — Implementation Plan

> Three-mode sensory toy for 15–24 month toddlers on the ESP32-C6-Touch-AMOLED-1.8.
> Combines IMU (QMI8658), AMOLED display (LVGL), and speaker output (ES8311 + NS4150B)
> for interactive cause-and-effect play.
>
> Date: 2026-04-05

---

## Modes

| # | Mode | Input | Output |
|---|------|-------|--------|
| 1 | **Snow Globe** | Shake → scatter, Tilt → gravity drift, Still → settle | Particles + tinkling sounds |
| 2 | **Dance Party** | Music plays, Motion intensity → visual intensity | Color bursts + music |
| 3 | **Music Maker** | Shake → maracas, Tap screen → drums, Tilt → pitch | Instrument sounds + ripples |

**Mode switching:** BOOT button (GPIO 9) rotates through modes 1→2→3→1.
**No auto-rotate** — display stays portrait.
**Session timer:** 3-minute auto-timeout per session, gentle fade to idle screen.

---

## Architecture

```
┌──────────────────────────────────────────────────┐
│                    main.c                         │
│  NVS → amoled_init → touch → LVGL → IMU → audio │
│  → mode_manager → lvgl_task loop                 │
└──────────────┬───────────────────────────────────┘
               │
    ┌──────────┼──────────┬───────────┐
    ▼          ▼          ▼           ▼
┌────────┐ ┌────────┐ ┌──────────┐ ┌──────────────┐
│imu_mgr │ │audio   │ │particle  │ │mode_manager  │
│        │ │output  │ │engine    │ │              │
│ QMI8658│ │ ES8311 │ │ LVGL     │ │ state machine│
│ accel  │ │ DAC    │ │ canvas   │ │ btn handler  │
│ gyro   │ │ I2S TX ��� │ gravity  │ │ session timer│
│ shake  │ │ amp P7 │ │ collision│ │              │
│ tilt   │ │ synth  │ │          │ │ snow_globe   │
└────────┘ └────────┘ └──────────┘ │ dance_party  │
                                   │ music_maker  │
                                   └──────────────┘
```

### Data Flow

```
IMU task (30 Hz)
  → reads QMI8658 accel/gyro
  → computes: tilt_x, tilt_y, shake_magnitude, motion_energy
  → writes to shared volatile struct (imu_state_t)

Audio task (continuous)
  → reads from PCM ring buffer or synthesizer
  → writes 16-bit stereo to I2S TX DMA
  → modes push samples into ring buffer

LVGL timer (200ms / 5 Hz for mode logic, 30ms / 33 Hz for animation)
  → reads imu_state_t
  → calls active mode's update() function
  → mode updates particle positions, triggers sounds
  → LVGL redraws via flush callback
```

---

## File Structure

```
projects/14_sensory_play/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── sdkconfig.defaults.template
├── IMPLEMENTATION_PLAN.md
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml
    ├── Kconfig.projbuild        — (empty, no WiFi config needed)
    ├── main.c                   — App init, LVGL task, button handler
    ├── imu_manager.c / .h       — QMI8658 driver wrapper, motion features
    ├── audio_output.c / .h      — ES8311 DAC, I2S TX, speaker amp, playback
    ├── particle_engine.c / .h   — Particle system with gravity
    ├── mode_manager.c / .h      — Mode FSM, session timer, BOOT button
    ├── mode_snow_globe.c / .h   — Snow globe mode logic
    ├── mode_dance.c / .h        — Dance party mode logic
    ├── mode_music.c / .h        — Music maker mode logic
    └── sounds.h                 — Embedded PCM samples (const arrays)
```

---

## Hardware Unlocked (First Time)

| Peripheral | Address/Pin | Purpose |
|-----------|-------------|---------|
| **QMI8658 IMU** | I2C 0x6B | Accelerometer + gyroscope for tilt/shake/motion |
| **NS4150B Speaker Amp** | TCA9554 P7 | Audio output enable |
| **ES8311 DAC** | I2S DO (GPIO 23) | PCM audio playback to speaker |

### Speaker Amp Enable

The NS4150B Class-D amplifier is controlled by TCA9554 IO expander pin P7.
Currently, `amoled_init()` creates the IO expander handle as a local variable
and doesn't expose it. We need to:

1. Store the IO expander handle in `amoled.c` as a static
2. Add `amoled_get_io_expander()` getter to `amoled.h`
3. In this project, call `esp_io_expander_set_dir(exp, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT)`
   then `esp_io_expander_set_level(exp, IO_EXPANDER_PIN_NUM_7, 1)` to enable amp

---

## Dependencies

### idf_component.yml
```yaml
version: "1.0.0"
dependencies:
  espressif/esp_codec_dev: "^1.4.0"   # ES8311 codec control
  waveshare/qmi8658: "*"              # QMI8658 IMU driver
  idf: ">=5.5.0"
```

### Shared Component
- `amoled_driver` via `EXTRA_COMPONENT_DIRS` (display, touch, PMIC, I2C bus)

---

## Init Sequence

```
1. NVS flash init
2. BOOT button GPIO 9 init
3. amoled_init()          — I2C → PMIC → TCA9554 → QSPI → SH8601
4. amoled_touch_init()    — FT3168 touch
5. amoled_lvgl_init()     — LVGL display + touch input
6. imu_manager_init()     — QMI8658 on shared I2C bus (0x6B)
7. audio_output_init()    — I2S driver + ES8311 codec (DAC mode)
8. Speaker amp enable     — TCA9554 P7 = HIGH
9. mode_manager_init()    — Create mode UIs, set initial mode
10. Start LVGL task       — Polls button, runs lv_timer_handler()
11. Start IMU task        — 30 Hz accelerometer/gyro reading
12. Set brightness        — amoled_set_brightness(180)
```

No WiFi. No NTP. No logging. Clean and focused.

---

## IMU Manager (imu_manager.c)

### Init
```c
qmi8658_init(&dev, amoled_get_i2c_bus(), QMI8658_ADDRESS_HIGH);  // 0x6B
qmi8658_set_accel_range(&dev, QMI8658_ACCEL_RANGE_4G);
qmi8658_set_accel_odr(&dev, QMI8658_ACCEL_ODR_125HZ);
qmi8658_set_gyro_range(&dev, QMI8658_GYRO_RANGE_512DPS);
qmi8658_set_gyro_odr(&dev, QMI8658_GYRO_ODR_125HZ);
```

### Shared State (volatile, read by LVGL timer)
```c
typedef struct {
    float accel_x, accel_y, accel_z;   // m/s², gravity-included
    float gyro_x, gyro_y, gyro_z;     // rad/s
    float tilt_x, tilt_y;             // normalized -1.0 to 1.0 from gravity vector
    float shake_magnitude;             // high-pass filtered accel magnitude
    float motion_energy;               // rolling average of |accel - gravity|
    bool  is_shaking;                  // shake_magnitude > threshold
    int64_t last_update_us;
} imu_state_t;
```

### Shake Detection
```c
// High-pass filter: subtract gravity estimate (low-pass filtered accel)
gravity = 0.95 * gravity + 0.05 * raw_accel;  // EMA low-pass
dynamic = raw_accel - gravity;                  // high-pass residual
shake_mag = sqrt(dx*dx + dy*dy + dz*dz);
is_shaking = shake_mag > 2.0;  // threshold in m/s²
```

### Tilt Calculation
```c
// Gravity vector gives device orientation
tilt_x = accel_x / 9.81;  // clamp to [-1, 1]
tilt_y = accel_y / 9.81;
```

### Task
- 30 Hz polling (33ms period)
- Stack: 4096 bytes
- Priority: 3

---

## Audio Output (audio_output.c)

### Architecture
```
Tone/PCM Generator → Ring Buffer (4096 samples) → I2S TX DMA → ES8311 DAC → NS4150B Amp → Speaker
```

### I2S Configuration
Same as project 12 (16 kHz, 16-bit, stereo), but we actually write to the TX channel:
```c
i2s_channel_write(s_tx_handle, stereo_buf, bytes, &written, timeout);
```

### ES8311 DAC Configuration
Project 12 already configures `ESP_CODEC_DEV_WORK_MODE_BOTH` and enables both TX+RX channels.
For output, we additionally:
```c
esp_codec_dev_set_out_vol(codec, 60);  // DAC volume 0-100
```

### Sound Types

1. **Synthesized tones** — sine wave generator with frequency + envelope (ADSR)
   ```c
   sample = (int16_t)(amplitude * sinf(2*PI * freq * t / SAMPLE_RATE));
   ```

2. **Noise** — LFSR pseudo-random for tambourine/shaker sounds
   ```c
   lfsr = (lfsr >> 1) ^ (-(lfsr & 1) & 0xB400);
   sample = (int16_t)(lfsr & 0x7FFF) - 16384;
   ```

3. **Embedded PCM clips** — Short drum hits, chimes stored as const int16_t arrays in sounds.h
   - Keep total embedded audio < 100KB to fit in flash without impacting app partition
   - Each sound: ~0.3s × 16000 Hz × 2 bytes = ~10 KB per clip

### Playback API
```c
void audio_output_play_tone(float freq_hz, int duration_ms, float volume);
void audio_output_play_noise(int duration_ms, float volume);
void audio_output_play_sample(const int16_t *pcm, size_t len);
void audio_output_set_music(bool playing);  // continuous music for dance mode
```

### Speaker Amp Control
```c
esp_io_expander_handle_t exp = amoled_get_io_expander();
esp_io_expander_set_dir(exp, IO_EXPANDER_PIN_NUM_7, IO_EXPANDER_OUTPUT);
esp_io_expander_set_level(exp, IO_EXPANDER_PIN_NUM_7, 1);  // amp ON
// ... when done:
esp_io_expander_set_level(exp, IO_EXPANDER_PIN_NUM_7, 0);  // amp OFF (save power)
```

---

## Particle Engine (particle_engine.c)

### Particle Structure
```c
#define MAX_PARTICLES  40

typedef struct {
    float x, y;         // position (pixels)
    float vx, vy;       // velocity (pixels/frame)
    float life;         // 0.0 = dead, 1.0 = full life
    float decay;        // life reduction per frame
    lv_color_t color;
    uint8_t radius;     // draw size
} particle_t;
```

### Physics (called every frame, ~30 Hz)
```c
for each particle:
    // Apply gravity from IMU tilt
    p->vx += imu.tilt_x * GRAVITY_SCALE;
    p->vy += imu.tilt_y * GRAVITY_SCALE;
    
    // Apply damping (friction)
    p->vx *= 0.98;
    p->vy *= 0.98;
    
    // Update position
    p->x += p->vx;
    p->y += p->vy;
    
    // Bounce off walls
    if (p->x < 0 || p->x > SCREEN_W) p->vx *= -0.6;
    if (p->y < 0 || p->y > SCREEN_H) p->vy *= -0.6;
    clamp(p->x, 0, SCREEN_W);
    clamp(p->y, 0, SCREEN_H);
    
    // Decay life
    p->life -= p->decay;
    if (p->life <= 0) mark_dead;
```

### Rendering
Use an LVGL canvas widget (full-screen) with direct pixel drawing:
```c
lv_canvas_fill_bg(canvas, bg_color, LV_OPA_COVER);  // clear
for each alive particle:
    lv_canvas_draw_rect(canvas, p->x - r, p->y - r, 2*r, 2*r, &draw_dsc);
```

Alternative: Use `lv_obj` per particle (simpler, no canvas, but limited to ~20 particles).
**Decision:** Use individual `lv_obj` circles — simpler, LVGL handles dirty regions
efficiently, and 40 small objects is fine for LVGL 8.4.

### Spawning
```c
void particle_spawn(float x, float y, int count, lv_color_t color, float speed);
// Creates `count` particles at (x,y) with random velocities within `speed` range
```

---

## Mode Manager (mode_manager.c)

### Mode Interface
```c
typedef struct {
    const char *name;
    lv_color_t bg_color;
    void (*init)(lv_obj_t *parent);    // create LVGL objects
    void (*enter)(void);                // called when mode becomes active
    void (*update)(const imu_state_t *imu, int64_t now_us);  // called from LVGL timer
    void (*on_touch)(int16_t x, int16_t y);  // touch event
    void (*exit)(void);                 // called when leaving mode
} mode_t;
```

### State Machine
```
         BOOT button
IDLE ←────────────────→ SNOW_GLOBE ←→ DANCE_PARTY ←→ MUSIC_MAKER
  ↑                         │               │              │
  └─── 3-min timeout ───────┴───────────────┴──────────────┘
```

### Session Timer
- 3-minute countdown per mode activation
- Last 10 seconds: particles fade, sounds reduce volume
- At timeout: return to idle screen (mode name + "tap to play" or "press button")
- Button press restarts timer in current mode

### BOOT Button
- Press cycles: Snow Globe → Dance Party → Music Maker → Snow Globe → ...
- Each press resets the 3-minute session timer
- Display shows brief mode transition animation (mode icon for 1 second)

### LVGL Timer Frequencies
- **Animation timer (33ms / 30 Hz):** Update particle positions, draw frame
- **Mode logic timer (200ms / 5 Hz):** Check session timer, update mode state

---

## Mode Details

### Mode 1: Snow Globe
- **Background:** Deep blue (#0a0a2e)
- **Particles:** White/cyan/light blue, 3-6px radius, slow decay
- **Shake:** Spawns 20 particles at center with random velocities
- **Tilt:** Gravity pulls particles in tilt direction
- **Still:** Particles gradually settle at bottom, twinkle effect
- **Sound:** Soft chime on shake, gentle tinkling while particles move
- **Feel:** Calm, mesmerizing, wind-down appropriate

### Mode 2: Dance Party
- **Background:** Black, with color cycling
- **Auto-play music:** Simple repeating melody using synthesized tones
- **Motion response:** `motion_energy` from IMU scales:
  - Number of particles spawned per frame
  - Particle size and brightness
  - Background color saturation
- **Shake:** Burst of rainbow particles + cymbal crash sound
- **Sound:** Continuous melody (C major pentatonic, ~120 BPM)
- **Feel:** Energetic, rewarding movement, joy

### Mode 3: Music Maker
- **Background:** Dark purple (#1a0a2e)
- **Screen regions:** Top-left, top-right, bottom-left, bottom-right = 4 "drum pads"
- **Tap:** Plays drum sound for that region + ripple animation from touch point
  - Top-left: kick drum (low tone ~100 Hz)
  - Top-right: snare (noise burst)
  - Bottom-left: hi-hat (short high noise)
  - Bottom-right: bell/chime (sine ~800 Hz)
- **Shake:** Maracas sound (filtered noise) + scatter particles
- **Tilt:** Pitch-bends the last played tone
- **Sound:** Each interaction produces an immediate sound
- **Feel:** Creative, cause-and-effect, exploratory

---

## RAM Budget

```
LVGL draw buffers (×2):   66 KB   (368×45×2 × 2)
LVGL heap:                48 KB   (CONFIG_LV_MEM_SIZE_KILOBYTES=48)
I2S TX DMA:                4 KB   (double buffer)
Audio ring buffer:         8 KB   (4096 samples × 2 bytes)
Synthesizer state:         1 KB
IMU state + task:          5 KB   (4K stack + state)
Particle objects:          4 KB   (40 × lv_obj overhead)
Mode state:                2 KB
FreeRTOS tasks:           20 KB   (LVGL 8K + IMU 4K + audio 4K + main 4K)
Sound samples (flash→RAM): 0 KB  (read directly from flash, not copied)
Misc:                      5 KB
───────────────────────────────────
Total:                   ~163 KB
Free:                    ~289 KB  (no WiFi!)
```

Very comfortable. Could go up to 48 KB LVGL heap if animations need it.

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
CONFIG_LV_FONT_MONTSERRAT_36=y
CONFIG_LV_MEM_SIZE_KILOBYTES=48
CONFIG_LV_COLOR_16_SWAP=y

# FreeRTOS
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_UNICORE=y

# No Bluetooth (save RAM)
CONFIG_BT_ENABLED=n

# Watchdog
CONFIG_ESP_TASK_WDT_TIMEOUT_S=15
```

### partitions.csv
```
# Name,     Type, SubType, Offset,   Size,   Flags
nvs,        data, nvs,     ,         0x6000,
phy_init,   data, phy,     ,         0x1000,
factory,    app,  factory, ,         3M,
```

No SPIFFS needed (no logging). 3 MB app partition.

---

## Build Steps

```bash
. ~/esp/esp-idf/export.sh
cd projects/14_sensory_play
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/cu.usbmodem3101 flash monitor
```
