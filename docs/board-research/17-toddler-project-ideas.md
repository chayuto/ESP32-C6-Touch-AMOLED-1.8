# Project Ideas for 15–24 Month Toddlers

> Firmware project proposals for the Waveshare ESP32-C6-Touch-AMOLED-1.8 board,
> designed around developmental milestones and evidence-based parenting research.
>
> Hardware: 1.8" 368×448 AMOLED touchscreen, ES8311 mic+speaker, QMI8658 6-axis IMU,
> PCF85063 RTC, AXP2101 PMIC, WiFi 6 / BLE 5, 452 KB DRAM, 16 MB flash.
>
> Companion research document: [16-toddler-product-research.md](16-toddler-product-research.md)
>
> Date: 2026-04-05

---

## Table of Contents

1. [Design Philosophy](#1-design-philosophy)
2. [Project A: Smart OK-to-Wake Clock + Sound Machine](#2-project-a-smart-ok-to-wake-clock--sound-machine)
3. [Project B: Toddler Activity & Sleep Tracker](#3-project-b-toddler-activity--sleep-tracker)
4. [Project C: Nursery Sound & Environment Monitor](#4-project-c-nursery-sound--environment-monitor)
5. [Project D: Musical Rhythm Toy](#5-project-d-musical-rhythm-toy)
6. [Project E: Visual Routine Helper](#6-project-e-visual-routine-helper)
7. [Project F: Calm-Down Breathing Guide](#7-project-f-calm-down-breathing-guide)
8. [Project G: Interactive Cause-and-Effect Explorer](#8-project-g-interactive-cause-and-effect-explorer)
9. [Combined Vision: Smart Nursery Companion](#9-combined-vision-smart-nursery-companion)
10. [Hardware Fit Summary](#10-hardware-fit-summary)
11. [References](#11-references)

---

## 1. Design Philosophy

### AAP Screen Time Constraint

The AAP "Media and Young Minds" policy (2016) is the governing constraint:

- **Under 18 months:** Avoid digital media except video chat
- **18–24 months:** High-quality content only, co-viewed with caregiver
- **Over 24 months:** Limit to 1 hour/day

This means the device should **not** be designed as an engagement trap. Every
project below follows this principle:

| Mode | Description | Screen Role |
|------|-------------|-------------|
| **Parent-facing** | Monitoring, alerts, data logging | Dashboard, always-on ambient |
| **Ambient** | Nightlight, sound machine, clock | Minimal display, background role |
| **Brief interaction** | Routine check, breathing guide | 30-second purposeful engagement |

### Developmental Alignment

At 15–24 months, children are developing:

- **Cause and effect** understanding (press → response)
- **Routine awareness** (predictable sequences reduce anxiety)
- **Emotional co-regulation** with caregivers (naming feelings, calming)
- **Motor skills** (walking, running, climbing — and falling)
- **Language** (10–50+ words, animal sounds, simple instructions)
- **Independence** ("me do it!") — Montessori sensitive period

Research basis: CDC 2022 updated milestones (Zubler et al., *Pediatrics* 2022),
Montessori sensitive periods, Vygotsky's zone of proximal development.

---

## 2. Project A: Smart OK-to-Wake Clock + Sound Machine

**The strongest project idea — best hardware fit, clear market gap, highest
parental utility.**

### Concept

A bedside clock that uses color and simple animated imagery to signal sleep/wake
times, combined with a volume-safe sound machine for sleep support.

### Why This Matters

OK-to-wake clocks are one of the most recommended tools by pediatric sleep
consultants for toddlers 18–24 months. The concept is simple: a **color change**
at a parent-set time tells the child it's OK to get out of bed. This leverages
the toddler's emerging color recognition and cause-and-effect understanding.

Current market leaders (Hatch Rest+ ~$90, LittleHippo MELLA ~$50, Gro Clock ~$35)
use simple LEDs or small LCD screens. None have a high-quality AMOLED touchscreen,
and none are open/hackable.

### Features

| Feature | Hardware Used | Research Basis |
|---------|-------------|----------------|
| **Color wake signal** | AMOLED full-screen color | Behavioral operant conditioning — color as discriminative stimulus |
| **Animated face** | AMOLED graphics (LVGL) | MELLA uses facial expressions; REMI uses animated eyes |
| **White/pink noise** | ES8311 DAC + NS4150B speaker | Spencer et al. (1990): 80% of neonates fell asleep within 5 min with white noise |
| **Volume limiter** | Software gain control | AAP: ≤50 dB; Hugh-Jones et al. (2014): 3/14 infant machines exceeded 85 dBA on max |
| **Nap timer** | PCF85063 RTC alarm | Configurable 30/60/90 min with gentle wake transition |
| **Gradual transitions** | AMOLED brightness ramp | Dim over 10 min before sleep, brighten before wake |
| **Schedule programming** | Touch UI + WiFi web config | Weekday/weekend/nap schedules |
| **Nightlight mode** | AMOLED ultra-low brightness | Self-emitting OLED at register 0x51 = 1–5 (nearly off) |
| **Battery backup** | AXP2101 PMIC | Hatch Rest+ also has backup battery; maintains schedule during outages |

### Display States

```
SLEEP MODE (nightlight):              WAKE MODE:
+------------------+                  +------------------+
|                  |                  |                  |
|    (very dim)    |                  |   ☀  GOOD       |
|                  |                  |   MORNING!       |
|  dark blue or    |                  |                  |
|  off             |                  |  (bright green   |
|                  |                  |   background)    |
+------------------+                  +------------------+

ALMOST TIME (optional):               NAP MODE:
+------------------+                  +------------------+
|                  |                  |                  |
|   (warm amber    |                  |  (slow pulse     |
|    glow)         |                  |   dim blue)      |
|                  |                  |                  |
|  5 minutes...    |                  |  zzz...          |
|                  |                  |                  |
+------------------+                  +------------------+
```

### Sound Machine Sounds (stored in flash or SD)

1. White noise (generated algorithmically — LFSR or filtered random)
2. Pink noise (white noise + 1/f filter)
3. Rain
4. Ocean waves
5. Heartbeat (womb simulation)
6. Fan / air conditioner
7. 3–5 lullabies

**Critical safety:** Software volume cap at ~50 dB SPL measured at 30 cm.
Calibrate once with a phone dB meter app and hard-limit the DAC output level.

### Technical Implementation

- **RTC alarm:** PCF85063 countdown timer or alarm register triggers wake
- **Sound playback:** Raw 8-bit PCM at 16 kHz stored in flash (16 MB = ~16 min of audio)
- **AMOLED nightlight:** Brightness register 0x51 set to 1–3 for near-dark ambient glow
- **WiFi config:** Simple web server (captive portal or LAN page) for setting schedules
- **Power:** Deep sleep between events, RTC wake interrupt, ~52 µA idle

### Competitive Advantage

| vs. Hatch Rest+ ($90) | vs. MELLA ($50) | vs. Gro Clock ($35) |
|-----------------------|-----------------|---------------------|
| Better display (AMOLED vs LED) | Touch interface | Full-color animations |
| Open source / hackable | Richer graphics | Battery backup |
| Cry detection capable | IMU motion detection | Sound machine built in |
| Activity monitoring possible | WiFi + notifications | WiFi connectivity |

### Estimated Complexity

- **RAM:** ~250 KB (LVGL + audio playback + WiFi)
- **Difficulty:** Medium — audio playback is new, rest uses proven patterns
- **Timeline:** 2–3 weekends

---

## 3. Project B: Toddler Activity & Sleep Tracker

### Concept

A wearable or crib-mounted device that uses the 6-axis IMU to classify toddler
activities (sleeping, sitting, walking, running, being carried) and detect
concerning falls, with sleep/wake logging and WiFi-based parent notifications.

### Why This Matters

No commercial product combines IMU-based activity tracking with cry detection for
toddlers. The Owlet sock stops at 18 months. Camera-based trackers (Nanit) require
line of sight. A wearable IMU works anywhere — crib, stroller, car seat, playroom.

Trost et al. (2019) demonstrated 88.3% activity classification accuracy for
toddlers using a single waist-worn accelerometer, and 98.4% when combined with a
barometric pressure sensor.

### Features

| Feature | Hardware | Research Basis |
|---------|----------|----------------|
| **Sleep/wake detection** | QMI8658 accelerometer | Standard actigraphy — low motion = sleep, validated in adults/children |
| **Activity classification** | QMI8658 accel + gyro | Trost et al. (2019): walking, running, crawling, sitting, carried |
| **Fall detection** | QMI8658 SVM threshold | Bagala et al. (2012): threshold + FSM approach, 90–95% accuracy |
| **Fall + cry correlation** | IMU + ES8311 mic | High-impact fall followed by sustained crying = alert. Normal tumble + silence = ignore |
| **Sleep quality score** | IMU motion variance + RTC | Wake count, total sleep time, restlessness index |
| **Daily activity summary** | WiFi push / web dashboard | Active minutes, sleep duration, fall events, cry events |
| **Step counting** | QMI8658 pedometer (built-in) | The QMI8658 has hardware step counter — zero CPU cost |

### Activity Classification Algorithm

```
IMU data (30 Hz) → sliding window (2 sec = 60 samples)
     |
     ├── Feature extraction:
     │   - Mean acceleration (x, y, z)
     │   - Variance per axis
     │   - Signal Vector Magnitude (SVM) mean & peak
     │   - Dominant frequency (simple FFT or zero-crossing rate)
     │   - Orientation (gyroscope integration or accel gravity vector)
     │
     ├── Threshold classifier (simple, no ML needed):
     │   SVM_mean < 0.05g  →  sleeping / still
     │   SVM_mean < 0.3g   →  sitting / quiet play
     │   SVM_mean < 1.0g   →  walking
     │   SVM_mean > 1.0g   →  running / active play
     │   SVM_peak > 3.0g   →  possible fall → check for post-fall stillness + cry
     │   orientation: horizontal + low motion → lying in crib
     │
     └── State machine: smooth transitions, require N consecutive windows to change state
```

### Fall Detection (Threshold + FSM)

```
Normal → [SVM > 3g] → Possible Fall → [low motion 2s] → Confirm Fall
                                          ↓                    ↓
                                     [motion resumes]     [cry detected?]
                                          ↓                    ↓
                                      Cancel              ALERT parent
```

The key insight for toddlers: **most falls are harmless**. The differentiator is
what happens *after* — a concerning fall shows: high impact + extended stillness or
sustained crying. Normal tumbles show: moderate impact + immediate resumption of
activity.

### Placement Options

| Position | Pros | Cons |
|----------|------|------|
| **Crib mattress** | No contact with child, sleep tracking | No activity tracking when out of crib |
| **Clip-on (waist/chest)** | Best activity + fall detection accuracy | Must be toddler-proof attachment |
| **Bedside table** | Always accessible, nightstand mode | Limited to vibration-coupled detection |

### Data Logging

Reuse the unified CSV logging pattern from project 12 (SPIFFS + SD export):

```
timestamp,event,activity,svm_mean,svm_peak,orientation,steps,sleep_state,
fall_detected,cry_detected,batt_pct,...
```

### Estimated Complexity

- **RAM:** ~200 KB (LVGL minimal + IMU processing + WiFi)
- **Difficulty:** Medium — IMU reading is straightforward, classification is simple thresholds
- **Timeline:** 2 weekends
- **New challenge:** QMI8658 driver integration (I2C 0x6B, use SensorLib)

---

## 4. Project C: Nursery Sound & Environment Monitor

### Concept

A continuous nursery monitoring station that measures ambient sound levels (dB),
classifies sounds (silence / background / talking / crying / loud event), monitors
noise exposure safety, and sends WiFi alerts to parents. Extends the existing cry
detection from project 12 into a broader environmental monitoring tool.

### Why This Matters

The AAP recommends nursery noise ≤50 dB (hospital) to ≤60 dB (home). Hugh-Jones
et al. (2014, *Pediatrics*) found that 3 out of 14 commercial infant sound machines
exceeded 85 dBA at 30 cm — the occupational exposure limit. Parents have no way to
verify their nursery meets safe noise levels.

A 2025 *Scientific Reports* paper described IoT-based nursery monitoring systems
integrating noise, temperature, humidity, and gas sensors with cry detection — but
using expensive custom PCBs. This board can do the audio portion natively.

### Features

| Feature | Hardware | Safety Standard |
|---------|----------|-----------------|
| **Continuous dB monitoring** | ES8311 mic → RMS → dB SPL | AAP: ≤50 dB hospital, CDC: ≤60 dB home |
| **Sound classification** | FFT spectral analysis | Silence / ambient / speech / cry / loud event |
| **Noise exposure logging** | SPIFFS CSV + RTC timestamp | Cumulative exposure tracking |
| **Loud event alerts** | WiFi push notification | Alert when sustained >70 dB |
| **Cry detection** | Existing project 12 algorithm | Already proven on this hardware |
| **Sound machine safety check** | Mic monitors own speaker output | Verify speaker output ≤50 dB at distance |
| **Display: ambient dB meter** | AMOLED bar/arc gauge | Green (<50), yellow (50–60), red (>60) |
| **Night mode** | AMOLED ultra-dim | Show only dB level at minimal brightness |

### dB SPL Estimation

The ES8311 ADC outputs 16-bit PCM at 16 kHz. To estimate dB SPL:

```
RMS = sqrt(sum(samples^2) / N)
dB_relative = 20 * log10(RMS / reference)
dB_SPL ≈ dB_relative + calibration_offset
```

One-time calibration: play a known-level tone (e.g., 70 dB from a calibrated
source or phone app), record the RMS value, compute the offset. This gives
approximately ±3 dB accuracy — sufficient for safety monitoring.

### Display Layout

```
+----------------------------------+
|      Nursery Monitor             |
|                                  |
|         47 dB                    |  ← Large dB reading
|     [========............]       |  ← Color bar (green/yellow/red)
|                                  |
|  Sound: ambient                  |  ← Classification
|  Peak: 52 dB (3m ago)           |
|  Avg (1hr): 44 dB               |
|                                  |
|  Cry Events: 0   Last: --       |
|  Loud Events: 2  Last: 14:32    |
|                                  |
|  Batt: 78%   WiFi: -42 dBm      |
+----------------------------------+
```

### Estimated Complexity

- **RAM:** ~230 KB (LVGL + audio + WiFi)
- **Difficulty:** Low-Medium — builds on proven project 12 audio pipeline
- **Timeline:** 1–2 weekends (most audio infrastructure exists)
- **Reuse:** audio_capture.c, cry_detector.c (add dB calculation + sound classification)

---

## 5. Project D: Musical Rhythm Toy

### Concept

An interactive music toy that responds to the child's touch and motion with sounds,
rhythms, and visual feedback. Designed for brief, supervised, caregiver-shared play
sessions aligned with AAP guidelines.

### Why This Matters

Zentner & Eerola (2010, *PNAS*) demonstrated that infants as young as 5 months
engage in rhythmic movement to music, and **smiling correlates with better rhythmic
synchronization** — suggesting an intrinsic link between rhythm and positive affect.

Trainor et al. (2012, *Annals of the NY Academy of Sciences*) showed that active
music participation (vs. passive listening) in infants produced earlier brain
responses to musical tones and more positive social outcomes.

Bainbridge et al. (2021, *Nature Human Behaviour*) found that lullabies from any
culture relaxed infants — decreased heart rate, reduced pupil dilation — confirming
cross-cultural soothing properties of musical patterns.

### Interaction Modes

| Interaction | Input | Output | Developmental Benefit |
|-------------|-------|--------|----------------------|
| **Tap drum** | Touch screen region | Drum sound + ripple animation | Cause-and-effect, rhythm |
| **Shake rattle** | IMU high-frequency motion | Rattle/shaker sound + particle animation | Gross motor, cause-and-effect |
| **Tilt rain** | IMU tilt angle | Rain/water sound intensity varies + visual rain | Spatial reasoning, proprioception |
| **Animal tap** | Touch animal icon | Animal sound + name spoken | Language, vocabulary (10–50 words at this age) |
| **Piano keys** | Touch colored bars | Musical note + color flash | Pitch discrimination, color recognition |
| **Rhythm follow** | Beat plays, child taps along | Visual feedback on sync quality | Rhythmic entrainment (Zentner & Eerola) |

### Sound Generation

Two approaches:

1. **Pre-recorded samples** (simpler): Store WAV/PCM clips in flash. 16 MHz flash,
   16 kHz 8-bit audio = 16 MB supports ~16 minutes of unique sounds. Plenty for
   animal sounds, drum hits, musical notes, and a few songs.

2. **Synthesized tones** (more flexible): Generate sine waves, square waves, or
   simple ADSR envelopes in real-time. ESP32-C6 at 160 MHz can synthesize multiple
   voices in real-time. Enables dynamic pitch, tempo, and melody generation.

### Caregiver Co-Play Design

Per AAP guidance, this is designed as a **shared activity**, not solo screen time:

- Sessions auto-end after 3–5 minutes with a gentle wind-down animation
- No addictive reward loops — simple cause-and-effect, not gamification
- Parent guide on display: "Tap with your child!" "Name the animal!"
- Mode selection requires adult touch pattern (two-finger tap or long press)

### Display Example (Drum Mode)

```
+----------------------------------+
|                                  |
|     🔴          🔵               |
|   (kick)      (snare)           |
|                                  |
|     🟢          🟡               |
|   (hi-hat)    (cymbal)          |
|                                  |
|  ♪ ♪ ♪ ♪ ♪ ♪ ♪ ♪               |  ← rhythm visualization
|                                  |
|      Shake for tambourine!       |
+----------------------------------+
```

### Estimated Complexity

- **RAM:** ~260 KB (LVGL + audio synthesis/playback + IMU)
- **Difficulty:** Medium-High — audio synthesis is new, touch + IMU + audio coordination
- **Timeline:** 2–3 weekends
- **New challenge:** Real-time audio output through ES8311 DAC + speaker amp (TCA9554 P7)

---

## 6. Project E: Visual Routine Helper

### Concept

A Montessori-inspired visual routine display that shows large, simple icons for
daily routines (morning, mealtime, bedtime), using color-coded time blocks synced
to the RTC clock. Designed for brief glance-and-go interaction.

### Why This Matters

Montessori education emphasizes that toddlers thrive on **order and predictability**.
Visual routine charts are a cornerstone tool — even children who can't read can
follow a sequence of 3–5 icons representing steps in a routine.

Research on visual schedules in early childhood settings shows improved compliance,
reduced transition anxiety, and increased independence (Dooley et al., 2001;
Massey & Wheeler, 2000 — primarily studied in ASD populations but principles
apply broadly).

The CDC's 18-month milestones include "copies you doing chores" and "plays with
toys in simple ways" — toddlers at this age are actively learning sequences and
routines through imitation.

### Routine Modes

**Morning Routine (3–5 steps):**
```
[🌅 Wake Up] → [🪥 Brush Teeth] → [👕 Get Dressed] → [🥣 Breakfast] → [✨ Done!]
```

**Bedtime Routine (3–5 steps):**
```
[🛁 Bath] → [📖 Story] → [🪥 Teeth] → [😴 Goodnight] → [🌙 Sleep]
```

**Mealtime (simple):**
```
[🧼 Wash Hands] → [🍽️ Eat] → [🧹 Clean Up]
```

### Interaction

- Display shows current step with a large icon (fills most of 368×448 screen)
- Child (or parent) taps screen to advance to next step
- Each completion plays a brief celebration sound + short animation
- After last step: gentle summary screen, then returns to clock/ambient mode
- RTC triggers routine display at scheduled times (e.g., 7:00 AM morning routine)

### Display Layout

```
+----------------------------------+
|        Morning Routine           |
|         Step 2 of 4              |
|                                  |
|         [LARGE ICON]             |
|         🪥                       |
|                                  |
|       Brush Teeth!               |
|                                  |
|   [ ✓ ] [ ● ] [ ○ ] [ ○ ]       |  ← progress dots
|                                  |
|        tap to continue           |
+----------------------------------+
```

### Estimated Complexity

- **RAM:** ~220 KB (LVGL + icons + simple audio)
- **Difficulty:** Low-Medium — mostly UI work, proven LVGL patterns
- **Timeline:** 1–2 weekends
- **Asset creation:** Icon sprites (can use simple geometric shapes, no artist needed)

---

## 7. Project F: Calm-Down Breathing Guide

### Concept

A guided breathing tool that uses smooth AMOLED animations (pulsing circle, color
transitions) and gentle audio (soft tones, nature sounds) to help a toddler
co-regulate during emotional moments. Designed for caregiver-led use.

### Why This Matters

Self-regulation is the #1 developmental challenge of the 18–24 month period.
Tantrums peak at this age as children develop autonomy but lack the emotional
vocabulary and regulation skills to manage frustration.

Before self-regulation, children need **co-regulation** — a caregiver modeling
calm and guiding them through overwhelming moments (Incredible Years research;
CCRC emotional regulation framework). Simple breathing exercises work for children
as young as 2 years:

- **STAR breathing:** Stop, Take a deep breath, And Relax
- **Balloon breathing:** Visualize inflating/deflating a balloon
- **Flower/candle:** Smell the flower (in), blow out the candle (out)

The **Zones of Regulation** framework (Kuypers, 2011) uses four colors: Blue (low
energy/sad), Green (calm/ready), Yellow (heightened/frustrated), Red (extreme
emotion). While designed for age 4+, the color concepts can be simplified for
toddlers with caregiver guidance.

### Breathing Animation

```
INHALE (3 sec):                    EXHALE (4 sec):
+------------------+              +------------------+
|                  |              |                  |
|      .-"-.       |              |                  |
|    /       \     |              |       .-.        |
|   |         |    |              |      ( . )       |
|    \       /     |              |       '-'        |
|      '-.-'       |              |                  |
|                  |              |                  |
|   breathe in...  |              |   breathe out... |
+------------------+              +------------------+

Color transitions: Blue (calm) ← Yellow (activating) ← Red (intense)
Background slowly shifts from warm to cool colors during the session.
```

The circle grows smoothly during inhale (3 seconds) and shrinks during exhale
(4 seconds). The longer exhale activates the parasympathetic nervous system,
reducing heart rate and promoting calm. This is standard clinical practice in
pediatric anxiety management.

### Activation

- **Parent-triggered:** Long press to enter calm-down mode
- **IMU-triggered (optional):** If device detects sustained agitation pattern
  (high-variance motion for >30 seconds), display suggests calm-down mode
- **Auto-end:** Session runs for 1–2 minutes, then gentle transition to ambient mode

### Audio

- Soft sustained tone that rises during inhale, falls during exhale
- Optional nature sounds (rain, ocean) playing underneath
- Gentle chime at session end

### Estimated Complexity

- **RAM:** ~200 KB (LVGL animation + simple audio)
- **Difficulty:** Low — straightforward animation loop, no complex logic
- **Timeline:** 1 weekend
- **Key challenge:** Smooth circle animation at consistent frame rate while playing audio

---

## 8. Project G: Interactive Cause-and-Effect Explorer

### Concept

A supervised play toy that responds to touch and motion with animations and sounds,
designed around the cognitive milestone of cause-and-effect understanding that
develops at 12–18 months.

### Why This Matters

At 15–18 months, cause-and-effect understanding is fully establishing. Children
learn that their actions produce predictable results — this is the foundation for
all future problem-solving. The ACM CHI PLAY 2019 paper on interactive toys for
infants recommends:

1. **Transparent, one-step interactions** — one action, one result
2. **Full-body activity and exploration with all senses**
3. **Effects appropriate for children's abilities**
4. **Predictable with clear relation to real-world experiences**
5. **Allow for shared experience** between child and caregiver

### Modes

**1. Touch Explorer:**
- Tap anywhere → color splash + sound effect (different colors per region)
- Swipe → trail of sparkles follows finger
- Two-finger tap → special animation (for caregiver to trigger)

**2. Tilt World:**
- Tilt device → ball/object rolls on screen following gravity
- Shake → stars scatter from center
- Turn upside down → rain falls "up"

**3. Animal Discovery:**
- Display shows 4 animal icons
- Tap animal → full-screen animal image + name spoken + sound
- Rotate through sets: farm animals, jungle animals, ocean animals
- Targets the 15-month milestone: "points to familiar objects when named"

**4. Color & Shape:**
- Large color squares fill screen
- Tap → color name spoken ("Red!" "Blue!")
- Targets 18–24 month color recognition development

### Session Management (AAP Compliance)

- Auto-timeout after 3 minutes of interaction
- Gentle wind-down: animations slow, colors soften, "bye bye!" message
- 10-minute cooldown before reactivation
- Parent override: two-finger long press to extend or restart
- Usage logging: total interaction time per day, sent to parent dashboard

### Estimated Complexity

- **RAM:** ~270 KB (LVGL rich animations + audio + IMU)
- **Difficulty:** Medium — animation variety is the main effort
- **Timeline:** 2–3 weekends
- **Fun factor:** Highest of all projects — most rewarding to demo

---

## 9. Combined Vision: Smart Nursery Companion

The individual projects above can be combined into a unified firmware with
mode-switching, creating a single device that serves multiple roles throughout
the day:

```
06:30  [Sleep Mode]      ── AMOLED off, IMU sleep tracking, cry detection active
06:45  [Wake Transition] ── Gradual amber glow, sound machine fades
07:00  [OK to Wake!]     ── Green screen, cheerful chime
07:05  [Morning Routine]  ── Visual checklist: wake → teeth → dress → breakfast
07:15  [Activity Track]   ── Background IMU logging, ambient dB monitoring
09:00  [Nap Timer]        ── Sound machine on, dim nightlight, sleep tracking
10:30  [Wake Signal]      ── Gentle brightening, soft chime
12:00  [Mealtime Routine] ── Wash hands → eat → clean up
14:00  [Play Mode]        ── Brief interactive music/animal session (3 min max)
18:00  [Wind-Down]        ── Calm colors, quiet sounds, routine preview
18:30  [Bedtime Routine]  ── Bath → story → teeth → goodnight
19:00  [Sleep Mode]       ── Sound machine, nightlight, cry monitoring
```

### Mode Switching

- **RTC-scheduled:** Automatic transitions based on parent-configured daily schedule
- **Manual:** BOOT button cycles modes, or touch gesture
- **WiFi:** Parent changes mode from phone/browser
- **Automatic:** IMU detects sleep/wake transitions and adjusts

### Resource Sharing

All projects share the same base infrastructure:

| Component | Shared By |
|-----------|-----------|
| LVGL display driver | All projects |
| ES8311 audio pipeline | A, C, D, F, G |
| QMI8658 IMU driver | B, D, F, G |
| PCF85063 RTC | A, B, C, E |
| AXP2101 battery monitoring | All projects |
| WiFi + web server | A, B, C |
| SPIFFS logging | B, C |
| Cry detection | A, B, C |

### RAM Budget (Combined)

```
LVGL display:          165 KB (draw buffers + heap)
Audio pipeline:         20 KB (I2S DMA + ring buffer)
WiFi stack:             55 KB
IMU processing:          2 KB
RTC driver:              1 KB
Mode-specific logic:    20 KB (only active mode loaded)
FreeRTOS tasks:         30 KB
Misc:                   10 KB
─────────────────────────────
Total:                ~303 KB of 452 KB
Free:                 ~149 KB
```

Feasible — but tight. Mode-specific assets (icons, sound data) can be loaded
from flash on demand rather than kept in RAM.

---

## 10. Hardware Fit Summary

| Project | Display | Touch | Mic | Speaker | IMU | RTC | WiFi | Battery | Fit |
|---------|---------|-------|-----|---------|-----|-----|------|---------|-----|
| **A: OK-to-Wake** | ★★★ | ★★ | ★ | ★★★ | — | ★★★ | ★★ | ★★★ | Excellent |
| **B: Activity Tracker** | ★★ | ★ | ★★ | ★ | ★★★ | ★★★ | ★★★ | ★★★ | Excellent |
| **C: Sound Monitor** | ★★ | ★ | ★★★ | ★ | — | ★★ | ★★★ | ★★ | Excellent |
| **D: Music Toy** | ★★★ | ★★★ | ★ | ★★★ | ★★★ | — | — | ★★ | Good |
| **E: Routine Helper** | ★★★ | ★★★ | — | ★★ | — | ★★★ | ★ | ★★ | Good |
| **F: Breathing Guide** | ★★★ | ★ | — | ★★ | ★ | — | — | ★★ | Good |
| **G: Cause-Effect** | ★★★ | ★★★ | — | ★★★ | ★★★ | — | — | ★★ | Good |

**Recommended build order:** A → C → B → E → D → F → G

Rationale: A (OK-to-Wake) delivers the most parent utility and establishes audio
output patterns. C (Sound Monitor) extends existing cry detection. B (Activity
Tracker) introduces the IMU. Then layer in interactive features.

---

## 11. References

### Child Development

1. **Zubler JM, Wiggins LD, Macias MM, et al. (2022).** "Evidence-Informed Milestones
   for Developmental Surveillance Tools." *Pediatrics*, 149(3):e2021052138.
   DOI: [10.1542/peds.2021-052138](https://doi.org/10.1542/peds.2021-052138)

2. **WHO Multicentre Growth Reference Study Group (2006).** "WHO Motor Development
   Study: Windows of achievement for six gross motor development milestones."
   *Acta Paediatrica Supplement*, 450:86–95.
   DOI: [10.1111/j.1651-2227.2006.tb02379.x](https://doi.org/10.1111/j.1651-2227.2006.tb02379.x)

3. **CDC (2022).** "Learn the Signs. Act Early." Milestone checklists at 15, 18, and
   24 months. [cdc.gov/act-early/milestones](https://www.cdc.gov/act-early/milestones/index.html)

### Screen Time & Media

4. **AAP Council on Communications and Media (2016).** "Media and Young Minds."
   *Pediatrics*, 138(5):e20162591.
   DOI: [10.1542/peds.2016-2591](https://doi.org/10.1542/peds.2016-2591)

### Sound & Sleep

5. **Spencer JA, Moran DJ, Lee A, Talbert D (1990).** "White noise and sleep induction."
   *Archives of Disease in Childhood*, 65(1):135–137.
   DOI: [10.1136/adc.65.1.135](https://doi.org/10.1136/adc.65.1.135)

6. **Hugh SC, Wolter NE, Propst EJ, et al. (2014).** "Infant Sleep Machines and
   Hazardous Sound Pressure Levels." *Pediatrics*, 133(4):677–681.
   DOI: [10.1542/peds.2013-3617](https://doi.org/10.1542/peds.2013-3617)

7. **AAP Committee on Environmental Health (2023).** "Preventing Excessive Noise
   Exposure in Infants, Children, and Adolescents." *Pediatrics*, 152(5):e2023063753.
   DOI: [10.1542/peds.2023-063753](https://doi.org/10.1542/peds.2023-063753)

### Music & Rhythm

8. **Zentner M & Eerola T (2010).** "Rhythmic engagement with music in infancy."
   *Proceedings of the National Academy of Sciences*, 107(13):5768–5773.
   DOI: [10.1073/pnas.1000121107](https://doi.org/10.1073/pnas.1000121107)

9. **Trainor LJ, Marie C, Gerry D, Whiskin E, Unrau A (2012).** "Becoming musically
   enculturated: effects of music classes for infants on brain and behavior."
   *Annals of the New York Academy of Sciences*, 1252:129–136.
   DOI: [10.1111/j.1749-6632.2012.06462.x](https://doi.org/10.1111/j.1749-6632.2012.06462.x)

10. **Bainbridge CM, Bertolo M, et al. (2021).** "Infants relax in response to
    unfamiliar foreign lullabies." *Nature Human Behaviour*, 5:256–264.
    DOI: [10.1038/s41562-020-00963-z](https://doi.org/10.1038/s41562-020-00963-z)

### Activity Tracking & Fall Detection

11. **Trost SG, et al. (2019).** "Hip and Wrist-Worn Accelerometer Data Analysis
    for Toddler Activities." *International Journal of Environmental Research
    and Public Health*, 16(22):4544.
    DOI: [10.3390/ijerph16224544](https://doi.org/10.3390/ijerph16224544)

12. **Bagala F, Becker C, et al. (2012).** "Evaluation of Accelerometer-Based Fall
    Detection Algorithms on Real-World Falls." *PLoS ONE*, 7(5):e37062.
    DOI: [10.1371/journal.pone.0037062](https://doi.org/10.1371/journal.pone.0037062)

### Emotional Regulation & Montessori

13. **Kuypers L (2011).** *The Zones of Regulation.* Social Thinking Publishing.
    [zonesofregulation.com](https://zonesofregulation.com/)

14. **Frontiers in Education (2024).** "Beyond play: a comparative study of
    multi-sensory and traditional toys in child education."
    [frontiersin.org](https://www.frontiersin.org/journals/education/articles/10.3389/feduc.2024.1182660/full)

### Interactive Design

15. **ACM CHI PLAY (2019).** "Interactive Soft Toys for Infants and Toddlers."
    [dl.acm.org](https://dl.acm.org/doi/10.1145/3311350.3347147)

### Nursery Environment

16. **AAP (2022).** "Sleep-Related Infant Deaths: Updated 2022 Recommendations."
    *Pediatrics*, 150(1):e2022057990.
    DOI: [10.1542/peds.2022-057990](https://doi.org/10.1542/peds.2022-057990)

### Smart Nursery Systems

17. **Scientific Reports (2025).** "A novel smart baby cradle system utilizing IoT
    sensors and machine learning for optimized parental care." *Nature*.
    DOI: [10.1038/s41598-025-02691-8](https://doi.org/10.1038/s41598-025-02691-8)

### Commercial Products Referenced

- Hatch Rest+ 2nd Gen (~$90) — [hatch.co](https://www.hatch.co/rest-plus-second-gen)
- LittleHippo MELLA (~$50) — [littlehippo.com](https://littlehippo.com/products/mella)
- Tommee Tippee Gro Clock (~$35) — [tommeetippee.com](https://www.tommeetippee.com/en-us/product/groclock-sleep-trainer-clock-499051)
- Owlet Dream Duo (~$399) — [owletcare.com](https://owletcare.com/products/owlet-dream-sock)
- Nanit Pro (~$300) — [nanit.com](https://www.nanit.com/products/nanit-pro-camera)
- REMI by UrbanHello (~$100) — [amazon.com](https://www.amazon.com/UrbanHello-REMI-Time-Rise-Communication/dp/B016IKZKKU)
