# Baby Cry Detection on a $20 Microcontroller: No ML Required

*How I built a real-time baby cry detector using pure DSP on an ESP32-C6 — and what I learned about the surprisingly distinctive acoustics of infant crying.*

---

When my partner and I started looking at baby monitors, the options fell into two buckets: dumb audio relays with awful latency, or $300+ "smart" monitors running proprietary cloud ML that phones home with your nursery audio. Neither sat well with me.

So I did what any embedded engineer would do — I built one from scratch on a $20 microcontroller.

This post covers the signal processing behind baby cry detection, why you might not need machine learning for it, and the use cases that emerge once you have reliable cry detection running at the edge.

## The Hardware

I used the [Waveshare ESP32-C6-Touch-AMOLED-1.8](https://www.waveshare.com/esp32-c6-touch-amoled-1.8.htm) — a surprisingly capable little board:

- **ESP32-C6**: RISC-V core at 160 MHz, WiFi 6, BLE 5.0
- **1.8" AMOLED**: 368x448 pixels — gorgeous for a status display
- **ES8311 audio codec**: 16-bit ADC with onboard PDM mic
- **AXP2101 PMIC**: LiPo battery management built in
- **Capacitive touch**: For brightness control and settings

No PSRAM. No FPU. No hardware DSP accelerator. 452 KB of RAM total, about half of which gets eaten by WiFi and display buffers. This is the kind of constraint that makes the problem interesting.

## Why Baby Crying Is Actually Easy to Detect (Acoustically)

Before writing a single line of code, I spent time in the literature. The acoustic properties of infant crying have been studied since the 1960s, and the findings are remarkably consistent.

Baby crying has a **spectral signature that is almost unique among everyday sounds**:

| Property | Baby Cry | Adult Speech | Background Noise |
|----------|----------|-------------|-------------------|
| Fundamental frequency (F0) | 350–600 Hz | 85–255 Hz | Broadband |
| Harmonic structure | Strong, regular harmonics up to 3.5 kHz | Complex, irregular formants | None |
| Temporal pattern | 0.5–2s cry bursts with pauses (~1 Hz cycle) | Continuous, variable rhythm | Continuous |
| Energy distribution | Concentrated in 250–600 Hz band | Spread across 300–5000 Hz | Flat/broadband |

That 350–600 Hz fundamental frequency is the key insight. It sits *above* almost all adult male speech and *above* most environmental noise, but *below* the range where things like alarms and ringtones live. Combined with the strong harmonic structure (a baby's vocal tract is essentially a simple resonant tube), you get a signal that's quite distinct from just about everything else in a home environment.

The cry-pause pattern is the other giveaway. Babies don't cry continuously — they cry in bursts separated by brief inhalation pauses, creating a characteristic ~1 Hz periodicity. Adults don't do this. TVs don't do this. Dogs don't do this.

## The Detection Pipeline

Here's the architecture I landed on after several iterations:

```
[Microphone] → [16 kHz / 16-bit ADC] → [I2S DMA Ring Buffer]
                                              |
                                    [Adaptive Energy VAD]
                                         |         |
                                      silence    active
                                         |         |
                                       skip    [512-pt FFT]
                                                   |
                                           [Hard Gates]
                                              |    |
                                            fail  pass
                                              |    |
                                          score=0  |
                                         [Multi-Feature Scoring]
                                                   |
                                         [Temporal State Machine]
                                                   |
                                           [Alert / Log / Display]
```

### Stage 1: Adaptive Energy Gate

Most of the time, a nursery is quiet. There's no reason to burn CPU cycles running FFTs on silence. An energy-based Voice Activity Detector (VAD) watches the RMS level against an adaptive noise floor. When energy exceeds the floor by 1.5x, the analysis pipeline wakes up. When the room is quiet, CPU usage drops to about 1%.

I originally wanted to use Espressif's ESP-SR VADNet for this stage, but it turns out VADNet is bundled inside their Audio Front-End pipeline, which only supports ESP32, ESP32-S3, and ESP32-P4. Not C6. Energy-based VAD turned out to be more than sufficient anyway — baby crying is a *loud* signal.

### Stage 2: FFT Analysis

When the VAD fires, I run a 512-point FFT with a Hanning window and 50% overlap — giving 32ms frames with 31.25 Hz frequency resolution. That resolution is fine for resolving the cry band (350–600 Hz spans about 8 FFT bins).

### Stage 3: Hard Gates

Before scoring, three hard gates must pass simultaneously:

1. **RMS gate**: Signal must be 2.5x above noise floor (reject near-silence that slipped through VAD)
2. **Cry ratio gate**: Energy in the cry band must exceed 8% of total (reject sounds with no cry-band content)
3. **Low-frequency rejection**: Energy below 250 Hz must be less than 35% of total (reject bass-heavy adult speech and music)

If any gate fails, the frame gets a score of zero. No exceptions. This is the single biggest thing that reduced false positives — the hard gates eliminate entire categories of sound before the scoring system even looks at them.

### Stage 4: Multi-Feature Scoring

Frames that pass all three gates get scored on a 0–100 weighted scale:

- **Cry band energy ratio** (350–550 Hz): up to 30 points
- **F0 + harmonic verification**: 20 points — checks that a detected fundamental has harmonics at 2x and 3x with expected amplitude ratios (2nd harmonic >= 35% of F0, 3rd >= 20%)
- **Low-frequency rejection bonus**: 20 points — rewards signals with minimal sub-250 Hz energy
- **Formant energy** (1–3.5 kHz): 10 points — the upper harmonics that give crying its piercing quality
- **Cry-pause periodicity**: 10 points — counts energy transitions to detect the characteristic burst pattern
- **Spectral crest factor**: 5 points — measures tonality (crying is tonal, noise is flat)
- **F0 stability**: 5 points — fundamental frequency should be stable across consecutive frames

### Stage 5: Temporal State Machine

A single high-scoring frame doesn't trigger an alert. The state machine requires **4 consecutive positive frames** (about 6 seconds of sustained crying) to trigger, and **4 consecutive negative frames** to clear. Hysteresis prevents oscillation: trigger threshold is 65/100, clear threshold is 45/100.

This means a brief shriek, a cough, or a TV sound effect won't trigger an alert. Real crying persists.

## What About Machine Learning?

The obvious question: why not just train a CNN?

I actually researched this extensively. A small depthwise-separable CNN on mel spectrograms could hit 92–96% accuracy in about 30–50 KB of flash and 40–60 KB of RAM using TFLite Micro with INT8 quantization. That's entirely feasible on the ESP32-C6.

But the pure DSP approach has compelling advantages for this specific problem:

**No training data required.** I don't need thousands of labeled cry samples. The detection is based on known acoustic properties of infant crying that have been validated across decades of research (Michelsson et al., 2002; Torres et al., 2017; Golub & Corwin, 1985).

**Fully interpretable.** When the detector triggers, I can look at the score breakdown and see *exactly why*. "Cry band ratio 0.42, harmonics verified at 480/960/1440 Hz, periodicity 7 transitions." Try getting that from a neural network.

**Zero-shot generalization.** The acoustic properties of baby crying are remarkably consistent across healthy infants. There's no domain adaptation problem, no need to fine-tune for "my baby's cry."

**Auditable.** Every parameter maps to a published acoustic finding. The 350–600 Hz range comes from Michelsson's meta-analysis. The harmonic ratios come from Golub & Corwin's physioacoustic model. The periodicity detection comes from Torres' HCBC framework. If someone questions a design decision, I can point to the paper.

The tradeoff? Pure DSP tops out around 92–94% accuracy with careful tuning, while a good CNN can push to 96%+. For a baby monitor where false negatives matter more than false positives, that gap is worth considering. My current approach: tune the DSP for high sensitivity (catch everything) and accept slightly more false positives, which the temporal state machine filters out anyway.

## The Display

One of the more satisfying parts of this project: the AMOLED display shows a real-time FFT spectrum with the cry frequency band highlighted in orange. You can literally *see* the harmonic structure of a cry lighting up the expected bins. It's also a great debugging tool — when the detector fires on a non-cry sound, the spectrum immediately shows you why.

The display also shows battery percentage, WiFi status, noise floor level, detection statistics, and the last cry timestamp. All rendered with LVGL on a 368x448 AMOLED that draws almost nothing when showing mostly black pixels (thanks, OLED).

## Use Cases Beyond "Is the Baby Crying?"

Once you have reliable cry detection running at the edge, interesting applications emerge:

### 1. Smart Nursery Automation

Trigger actions when crying is detected: turn on a dim nightlight, start playing white noise, send a notification to a parent's phone. Crucially, all detection happens on-device — no audio leaves the nursery.

### 2. Cry Pattern Logging & Analytics

The firmware logs every detection event with timestamp, duration, and score breakdown to onboard flash (SPIFFS) with SD card export. Over weeks, you can see patterns: *Does the baby cry more at certain times? Is the 2 AM waking getting shorter? Are naps consistent?* Pediatricians actually find this data useful.

### 3. Caregiver Alert Systems

In daycare or NICU settings, a network of these devices could provide per-crib cry detection with centralized alerting. At $20 per node, it's orders of magnitude cheaper than commercial solutions. The WiFi notification pipeline is already built — extending to MQTT or HTTP webhooks is straightforward.

### 4. Elderly Care & Distress Detection

The same acoustic principles — high-pitched vocalization, temporal patterns, energy above ambient — apply to detecting distress vocalizations from elderly individuals. The frequency ranges would need adjustment (adult distress calls are lower, around 200–400 Hz), but the architecture transfers directly.

### 5. Pet Monitoring

Dog whining and cat distress vocalizations have their own characteristic frequency profiles. A pet-specific version could detect distress, excessive barking, or separation anxiety vocalizations while the owner is away.

### 6. Privacy-Preserving Audio Classification

This is perhaps the most important use case philosophically. The device never records, stores, or transmits audio. It extracts frequency-domain features, computes a score, and discards the raw audio within milliseconds. You get the *meaning* of the sound without the *content*. In an era of always-listening smart speakers, there's real value in audio intelligence that is architecturally incapable of eavesdropping.

## Performance & Resource Usage

Final numbers on the ESP32-C6:

| Metric | Value |
|--------|-------|
| CPU usage (quiet room) | ~1% (VAD only) |
| CPU usage (active detection) | ~5-9% |
| RAM usage | ~231 KB of 452 KB |
| Flash (firmware) | ~1.2 MB |
| Detection latency | ~6 seconds (4 consecutive frames) |
| False positive rate | <2 per hour in typical home |
| Power consumption | ~80 mA active, battery lasts 6-8 hours |

The detection latency deserves a note: 6 seconds feels long on paper, but in practice, a parent's response time to crying is measured in minutes, not seconds. A 6-second confirmation window eliminates the vast majority of false positives while still alerting well before anyone would naturally wake up and respond.

## What I'd Do Differently

**Add a small CNN as a second opinion.** The pure DSP approach is solid, but a tiny INT8-quantized model running every 1.5 seconds as a parallel classifier could boost accuracy to 95%+ while keeping the DSP path as the primary detector. The ESP32-C6 can handle both — there's headroom.

**Use the IMU for context.** The board has a 6-axis IMU. If the device is mounted on a crib, accelerometer data could detect rocking or movement, providing additional context for the detection (baby crying + crib shaking = definitely awake).

**Build a mesh network.** Multiple ESP32-C6 devices with BLE mesh could provide whole-home coverage with location-aware alerting. "Baby is crying — in the nursery" vs. "Baby is crying — in the living room."

## The Takeaway

Baby cry detection is a problem where domain knowledge beats raw compute. The acoustic properties of infant crying are so well-characterized and so distinctive that a 160 MHz RISC-V chip with no FPU can detect it reliably using nothing but an FFT and some carefully chosen thresholds.

Not every audio classification problem needs a neural network. Sometimes, reading the literature and understanding the physics gets you 90% of the way there — and the last 10% isn't worth the complexity.

The full source code, detection methodology documentation with academic citations, and hardware setup guide are available in the project repository.

---

*Built with ESP-IDF 5.5 on the Waveshare ESP32-C6-Touch-AMOLED-1.8. Detection algorithm based on findings from Michelsson et al. (2002), Torres et al. (2017), Golub & Corwin (1985), and Peeters (2004).*
