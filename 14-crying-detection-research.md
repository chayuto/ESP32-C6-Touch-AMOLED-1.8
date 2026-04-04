# Crying Detection on ESP32-C6 — Deep Research

> Research into crying detection algorithms suitable for the Waveshare ESP32-C6-Touch-AMOLED-1.8 board.
> Date: 2026-04-04
> Constraints: 160MHz RISC-V single-core, 452KB DRAM, no PSRAM, ES8311 codec + dual PDM MEMS mics.
> Requirement: Keep AMOLED display + power management functional alongside detection.

---

## Table of Contents

1. [Why This Hardware Can Do It](#1-why-this-hardware-can-do-it)
2. [Acoustic Properties of Baby Crying](#2-acoustic-properties-of-baby-crying)
3. [Detection Algorithm Landscape](#3-detection-algorithm-landscape)
4. [ML Frameworks for ESP32-C6](#4-ml-frameworks-for-esp32-c6)
5. [Audio Feature Pipeline Design](#5-audio-feature-pipeline-design)
6. [RAM Budget Analysis](#6-ram-budget-analysis)
7. [Candidate Architectures](#7-candidate-architectures)
8. [Training Data and Datasets](#8-training-data-and-datasets)
9. [Recommendation](#9-recommendation)

---

## 1. Why This Hardware Can Do It

The ESP32-C6-Touch-AMOLED-1.8 has all the pieces:

| Component | Role in Cry Detection |
|-----------|----------------------|
| **ES8311 codec** (I2C 0x18) | Mono ADC, 16kHz/16-bit, 100dB SNR — digitizes mic input |
| **Dual PDM MEMS mics** | Audio capture (one mic through ES8311 DMIC interface) |
| **I2S peripheral** | Streams audio data: MCK=19, BCK=20, WS=22, DI=21 |
| **160MHz RISC-V CPU** | MFCC extraction <0.5ms/frame, CNN inference 50-100ms — both real-time |
| **452KB DRAM** | Enough for display + WiFi + audio + ML simultaneously |
| **SH8601 AMOLED** | Show detection status, battery level, alerts |
| **AXP2101 PMIC** | Battery monitoring, power management, long-press power off |
| **WiFi 6** | Push notifications when cry detected |

The key insight: baby cry detection is a **well-solved TinyML problem**. A quantized INT8 CNN classifier needs only 30-50KB flash and 40-60KB RAM — comfortably within budget.

---

## 2. Acoustic Properties of Baby Crying

Baby crying has distinctive acoustic signatures that make it classifiable even on low-resource hardware:

### Spectral Characteristics

| Feature | Crying | Adult Speech | Ambient Noise |
|---------|--------|-------------|---------------|
| **Fundamental freq (F0)** | 250–600 Hz (peak ~350-450 Hz) | 85–255 Hz | Broadband |
| **Harmonic structure** | Strong harmonics up to 3.5 kHz | Complex formants | No harmonics |
| **Spectral centroid** | Higher than speech | Moderate | Variable |
| **Spectral flatness** | Tonal (low flatness) | Semi-tonal | High (flat) |
| **Energy band** | Concentrated 250–3500 Hz | 300–5000 Hz | Broadband |

### Temporal Characteristics

| Feature | Description |
|---------|-------------|
| **Cry-pause pattern** | 0.5–2s cry bursts followed by brief inhalation pauses |
| **Duty cycle** | ~60–80% (mostly crying, short pauses) |
| **Periodicity** | Cry rhythm at ~0.5–1 Hz is highly distinctive |
| **Duration** | Cry episodes last 30s to several minutes |
| **Zero-crossing rate** | Moderate-to-high (~0.05–0.15 per sample at 16kHz) |

### Why This Matters for Algorithm Design

- The **high F0 range** separates crying from adult speech and most environmental sounds
- The **periodic cry-pause pattern** is a strong temporal feature not shared by alarms, music, or pets
- **16 kHz sample rate** (Nyquist = 8 kHz) captures all relevant spectral content
- **8 kHz sample rate** (Nyquist = 4 kHz) is also viable — captures F0 and first harmonics, loses some high harmonics. Typically 1-3% accuracy reduction.

---

## 3. Detection Algorithm Landscape

### 3.1 Classical DSP (No ML)

**Approach:** Extract spectral/temporal features → threshold-based rules

| Feature | Computation | RAM | Accuracy |
|---------|-------------|-----|----------|
| Energy threshold | ~N multiply + sum | Negligible | ~70% (many false positives) |
| Energy + F0 check | FFT + peak finding | ~4 KB | ~75–85% |
| Energy + F0 + periodicity | + autocorrelation | ~8 KB | ~80–88% |

**Pros:** Tiny RAM (<10 KB), near-zero inference time, easy to implement.
**Cons:** Poor false-positive rate. TV audio with baby crying, high-pitched alarms, and pets all trigger false detections. Not production-quality.

### 3.2 Classical ML on Handcrafted Features

**Approach:** MFCC extraction → SVM / Random Forest / GMM

| Classifier | Feature Input | Model Size | RAM | Accuracy | Inference |
|-----------|--------------|-----------|-----|----------|-----------|
| SVM (RBF) | 39 MFCCs | 2–10 KB | <20 KB | 89–93% | <1 ms |
| Random Forest (20 trees) | 13 MFCCs + spectral | 15–60 KB | <25 KB | 88–92% | <2 ms |
| GMM (4 components) | 39 MFCCs | 1–5 KB | <15 KB | 85–90% | <1 ms |
| k-NN (k=5) | 13 MFCCs | ~5 KB | <15 KB | 85–90% | 1–10 ms |

**Pros:** Very small models, fast inference, interpretable.
**Cons:** Feature engineering is manual. Less robust than learned features. Good for prototyping, not best for production.

### 3.3 Small CNN on Mel Spectrogram (TFLite Micro)

**Approach:** Mel spectrogram → quantized CNN → binary classification

This is the **state-of-the-art for embedded cry detection**.

| Architecture | Parameters | Flash (INT8) | RAM (inference) | Accuracy | Inference Time |
|-------------|-----------|-------------|-----------------|----------|---------------|
| 2-conv + dense | 5K–15K | 20–60 KB | 10–30 KB | 92–94% | 20–50 ms |
| 3-conv + dense | 20K–50K | 80–200 KB | 20–50 KB | 94–96% | 50–100 ms |
| DS-CNN-S (ARM ref) | ~24K | ~96 KB | ~30 KB | ~94% | 30–80 ms |
| Custom ResNet-tiny | ~30K | ~120 KB | ~40 KB | 94–96% | 50–150 ms |

**Pros:** Best accuracy/size tradeoff. Well-supported tooling (Edge Impulse, TFLite Micro). Learns features automatically — no manual feature engineering.
**Cons:** Requires training data and tooling. Slightly higher RAM than classical ML. Needs TFLite Micro runtime (~50-100 KB flash overhead).

### 3.4 Hybrid: DSP Pre-filter + CNN Classifier

**Approach:** Energy-based VAD (voice activity detection) → only run CNN when audio is active

This is the **recommended production architecture**:

```
Audio Stream (16kHz)
    │
    ▼
[Energy VAD] ─── silence ──► sleep (0 CPU)
    │
    audio active
    │
    ▼
[MFCC / Mel Spectrogram Extraction]
    │
    ▼
[Small CNN Classifier]
    │
    ├── "crying" (>threshold) ──► ALERT
    └── "not crying" ──► continue monitoring
```

**Why hybrid:** The VAD gate costs <1% CPU and prevents CNN inference during silence (which is 90%+ of the time in a typical nursery). This saves power and eliminates false positives from background noise during quiet periods.

---

## 4. ML Frameworks for ESP32-C6

### 4.1 TensorFlow Lite Micro (esp-tflite-micro) — RECOMMENDED

| Aspect | Detail |
|--------|--------|
| **ESP32-C6 support** | Yes — via `espressif/esp-tflite-micro` ESP-IDF component |
| **RISC-V compatible** | Yes — ANSI C reference kernels (no SIMD but functional) |
| **Flash footprint** | ~100-200 KB (TFLite runtime) + model size |
| **RAM footprint** | Depends on model — typically 10-60 KB for audio classifiers |
| **Inference speed** | 50-200ms for audio classification models on ESP32 |
| **Quantization** | INT8 quantization standard — 4x smaller, 2-4x faster |
| **Tooling** | Full pipeline: train in TF/Keras → convert to TFLite → quantize → deploy |

**Key component:** `espressif/esp-tflite-micro` on the ESP Component Registry.

### 4.2 ESP-SR (Espressif Speech Recognition)

| Aspect | Detail |
|--------|--------|
| **ESP32-C6 support** | Partial — WakeNet9s and VADNet only (MultiNet, AFE: NO) |
| **WakeNet9s RAM** | ~10 KB internal RAM, ~190 KB flash |
| **VADNet** | Neural VAD, trained on ~15K hours, supports C6 |
| **Purpose** | Wake word detection ("Alexa", "Hi ESP"), NOT general audio classification |
| **Custom wake words** | Possible via Espressif service, but designed for spoken phrases |

**Verdict:** WakeNet9s is optimized for phrase detection, not cry detection. The AFE (noise suppression, AEC) does NOT support C6 — only ESP32/S3/P4. However, **VADNet** (neural voice activity detection) does support C6 and could serve as a pre-filter. Note: ESP-SR has no sound event classifier — purely speech-focused.

### 4.3 ESP-DSP (Digital Signal Processing)

| Aspect | Detail |
|--------|--------|
| **ESP32-C6 support** | Yes — ANSI C reference implementations (no RISC-V SIMD optimizations for C6, those are P4-only) |
| **Functions** | FFT, filters, matrix ops, vector math |
| **Relevance** | MFCC computation uses FFT + mel filterbank |
| **Performance** | 512-pt FFT in ~0.15ms at 160MHz (estimate, unoptimized) |

**Verdict:** Use ESP-DSP for the feature extraction pipeline (FFT → mel filterbank → optional DCT for MFCCs). The ANSI C implementation is fast enough at 160MHz.

### 4.4 Edge Impulse SDK

| Aspect | Detail |
|--------|--------|
| **ESP32-C6 support** | Yes — confirmed by [community project](https://github.com/sl45sms/esp32c6-Keyword-Spotting) |
| **One-line patch** | Add `CONFIG_IDF_TARGET_ESP32C6` to `ei_classifier_porting.h` |
| **Pipeline** | End-to-end: data collection → feature extraction → model training → quantization → deployment |
| **Typical model** | 7-40 KB flash, 5-30 KB RAM, runs in 15-50ms |
| **EON compiler** | Reduces RAM ~27%, ROM ~42% vs standard TFLite |
| **Cry detection** | Published tutorials and audio anomaly detection templates |

**Verdict:** Excellent for training. Can export a standalone C library that includes MFCC extraction + CNN classifier. EON compiler produces highly optimized inference code. Alternatively, train elsewhere and deploy raw TFLite model.

### 4.5 emlearn (Classical ML in Pure C)

| Aspect | Detail |
|--------|--------|
| **ESP32-C6 support** | Yes — pure C99, platform-agnostic, no runtime needed |
| **Models** | Random Forest, Decision Tree, MLP, Naive Bayes (scikit-learn export) |
| **Size** | From 2 KB flash, 50 bytes RAM for simple classifiers |
| **No dynamic allocations** | Perfect for constrained MCUs |

**Verdict:** Great for Option A (classical ML prototype). Train a Random Forest on MFCC features in Python, export to plain C. Combine with ESP-DSP for MFCC extraction. Ultra-low resource cost.

### 4.6 Critical Hardware Constraints

| Constraint | Impact |
|-----------|--------|
| **No FPU** | Floating-point is emulated in software — very slow. INT8 quantization is essential, not optional. |
| **No SIMD** | No hardware acceleration for convolutions (unlike ESP32-S3). Keep models to 1-2 conv layers max. |
| **No PSRAM** | Full ESP-SR (AFE, MultiNet) requires PSRAM — cannot use. |
| **Single core** | All tasks share one core — audio capture via DMA is critical to avoid CPU blocking. |
| **16 MB flash** | Flash is generous — model storage is NOT the bottleneck. RAM is. |

### 4.7 Framework Comparison for Our Use Case

| Framework | Fit for Cry Detection | Effort | RAM Cost |
|-----------|----------------------|--------|----------|
| **Edge Impulse** | Best — batteries-included pipeline | Low (handles feature + model) | 5-30 KB |
| **TFLite Micro** | Best — flexible, well-supported | Medium (need own feature pipeline) | 30-65 KB |
| **emlearn + ESP-DSP** | Good for prototype (classical ML) | Low | 2-20 KB |
| **ESP-SR WakeNet9s** | Poor — designed for wake phrases | Low to adapt | ~10 KB |
| **Pure DSP (no ML)** | Prototype only | Very low | <10 KB |

---

## 5. Audio Feature Pipeline Design

### 5.1 Audio Capture

```
PDM MEMS Mic → ES8311 DMIC Interface → ES8311 ADC → I2S → ESP32-C6
```

- **Sample rate:** 16 kHz (captures F0 + harmonics up to 8 kHz)
- **Bit depth:** 16-bit (sufficient for cry detection, matches I2S standard mode)
- **ES8311 config:** Slave mode, analog mic input (BSP uses `digital_mic = false`), PGA gain 21-27 dB, HPF enabled, ALC enabled
- **I2S config:** Standard I2S (Philips), 16kHz, 16-bit, mono, full-duplex
- **ESP32-C6 has 1 I2S peripheral** — shared TX (speaker) and RX (mic)
- **DMA-based capture** — zero-copy audio streaming, no CPU intervention

**ESP-IDF component:** `espressif/esp_codec_dev` v1.4.0+ (unified codec API, includes ES8311 driver)

**Speaker amplifier:** NS4150B Class-D amp, enable via TCA9554 IO expander pin P7 (EXTO7)

**Important:** Waveshare BSP sets `digital_mic = false` — the board may route analog mic input through ES8311 PGA, not DMIC PDM interface. Verify with schematic.

### 5.2 Feature Extraction (Streaming)

Process audio frame-by-frame to minimize RAM:

| Parameter | Value | Notes |
|-----------|-------|-------|
| Frame size | 512 samples (32ms) | Standard for speech/cry analysis |
| Hop size | 256 samples (16ms) | 50% overlap, good time resolution |
| FFT size | 512 | Same as frame size |
| Window | Hanning | Standard for audio STFT |
| Mel bands | 40 | Standard; 20 viable if RAM-constrained |
| MFCCs | 13 | Optional — mel spectrogram may be fed directly to CNN |
| Analysis window | 1.5 seconds | ~94 frames — captures one cry-pause cycle |

### 5.3 Streaming vs Buffered

**Streaming (recommended):**
- Compute mel spectrogram frame-by-frame
- Store into circular 2D buffer: 40 mel bands × 94 frames = 15 KB (float) or 3.7 KB (int8)
- Run CNN on this buffer every 1.5 seconds
- Audio buffer: only 1 frame (1 KB) needed at a time
- **Total feature RAM: ~4–16 KB**

**Buffered (simpler but more RAM):**
- Store 1.5s of raw audio: 24,000 samples × 2 bytes = 48 KB
- Compute all features at once
- **Not recommended** — wastes 48 KB on raw audio storage

### 5.4 Processing Timeline

```
Every 16ms (hop = 256 samples at 16kHz):
  1. Read 256 new samples from I2S DMA buffer       ~0 ms (DMA)
  2. Apply Hanning window to 512-sample frame        ~0.05 ms
  3. 512-point FFT                                   ~0.15 ms
  4. Compute 40 mel band energies                    ~0.05 ms
  5. Store into mel spectrogram circular buffer       ~0 ms
  ─────────────────────────────────────────────
  Total per frame:                                   ~0.25 ms of 16ms budget (1.6% CPU)

Every 1.5 seconds:
  6. Run INT8 CNN on mel spectrogram buffer          ~50-100 ms
  7. Apply threshold + temporal smoothing             ~0 ms
  8. If crying detected → update display + WiFi alert ~1 ms
  ─────────────────────────────────────────────
  Total per inference:                               ~50-100 ms (3-7% CPU burst)

Overall CPU utilization: ~5-9% for feature extraction + inference
```

This leaves >90% of CPU for LVGL display rendering, WiFi, and other tasks.

---

## 6. RAM Budget Analysis

### 6.1 Current MCP Canvas Project (Reference)

```
LVGL draw buffers (DMA):  65 KB  (2× 368×45 RGB565)
LVGL heap pool:           64 KB
WiFi stack:              ~55 KB
Snapshot buffers (BSS):   51 KB  (RGB888 + JPEG)
FreeRTOS tasks:          ~25 KB
Misc:                    ~15 KB
────────────────────────
Total:                  ~275 KB of 452 KB
Free:                    ~55 KB  ← NOT enough for ML
```

### 6.2 Crying Detection Project (New Design)

We can't just add ML on top of the MCP canvas project — need a fresh RAM budget.

**Option A: Display + WiFi + Audio + ML (no MCP, no snapshot)**

```
LVGL draw buffers (DMA):  65 KB  (2× 368×45 RGB565) — keep full quality
LVGL heap pool:           32 KB  (simpler UI: status text + icons only)
WiFi stack:              ~55 KB  (STA mode for notifications)
I2S DMA buffers:           4 KB  (2× 1024-byte ring buffers)
Audio processing:
  FFT workspace:           4 KB  (512-pt complex float)
  Mel filterbank:          2 KB  (coefficients, precomputed)
  Mel spectrogram buf:    16 KB  (40 × 94 × float32, circular)
  OR int8 spectrogram:     4 KB  (40 × 94 × int8)
TFLite Micro runtime:    ~30 KB  (interpreter + arena)
ML model weights:          0 KB  (in flash, not RAM)
ML tensor arena:          20 KB  (activation buffers during inference)
FreeRTOS tasks:           20 KB  (LVGL 8KB + audio 4KB + WiFi 4KB + main 4KB)
ES8311 codec driver:       1 KB
Misc (cJSON, HTTP):       10 KB
────────────────────────────────
Total:                  ~259 KB  (with float mel spectrogram)
                        ~247 KB  (with int8 mel spectrogram)
Free:                   ~193-205 KB  ← COMFORTABLE
```

**Option B: Reduced Display Buffers (if more ML headroom needed)**

```
LVGL draw buffers:        33 KB  (1× 368×45 RGB565 — single buffer)
LVGL heap pool:           24 KB  (minimal UI)
WiFi stack:              ~55 KB
Audio + ML:              ~77 KB  (same as above)
FreeRTOS + misc:         ~31 KB
────────────────────────────────
Total:                  ~220 KB
Free:                   ~232 KB  ← VERY COMFORTABLE
```

### 6.3 Verdict

**No problem.** Even with full-quality LVGL double-buffered display, WiFi, audio capture, and ML inference, we use ~260 KB of 452 KB available. There's ~190 KB of headroom — enough for larger models or additional features.

The snapshot/JPEG buffers (51 KB in the MCP project) are not needed here, which alone frees significant space.

---

## 7. Candidate Architectures

### Architecture A: Edge Impulse Pipeline (Easiest Path)

```
[ES8311 ADC] → [I2S DMA] → [Edge Impulse MFCC] → [Edge Impulse CNN] → [Alert]
```

- Train model on Edge Impulse studio (web-based, free tier)
- Export as ESP-IDF C++ library
- Includes feature extraction + model + inference — turnkey
- **Model: ~30-80 KB flash, ~40-60 KB RAM**
- **Accuracy: ~92-95%**
- **Effort: Low** — most of the work is data collection and training

### Architecture B: TFLite Micro + Custom Feature Pipeline (Most Control)

```
[ES8311 ADC] → [I2S DMA] → [ESP-DSP FFT] → [Mel Filterbank] → [TFLite CNN] → [Alert]
```

- Write feature extraction in C using ESP-DSP
- Train CNN in Python (Keras/TF), export as TFLite INT8
- Deploy via `esp-tflite-micro` component
- **Model: ~30-50 KB flash, ~40-60 KB RAM**
- **Accuracy: ~92-96%**
- **Effort: Medium** — need to write feature pipeline + training script

### Architecture C: Hybrid DSP + Small CNN (Best Power Efficiency)

```
[ES8311 ADC] → [I2S DMA] → [Energy VAD] ─── quiet ──→ [sleep]
                                │
                            loud event
                                │
                                ▼
                        [MFCC extraction]
                                │
                                ▼
                         [Small CNN via TFLite]
                                │
                          ┌─────┴─────┐
                       crying      not crying
                          │            │
                     [ALERT]      [resume]
```

- VAD gate: ~1% CPU when quiet (which is most of the time)
- CNN only runs when audio detected — saves power
- **Same model cost as B, but better battery life**
- **Effort: Medium-High** — extra VAD stage
- **Recommended for battery-powered deployment**

### Architecture D: Pure DSP (Prototype / Minimal)

```
[ES8311 ADC] → [I2S DMA] → [FFT] → [Band Energy] → [F0 Detection] → [Threshold] → [Alert]
```

- No ML framework dependency
- Energy in 250-600 Hz band + periodicity check
- **Model: 0 KB (no model), ~10 KB working RAM**
- **Accuracy: ~80-88%** (too many false positives for production)
- **Effort: Low**
- Useful as **first step to validate audio pipeline** before adding ML

---

## 8. Training Data and Datasets

### Available Datasets

| Dataset | Size | Description | Access |
|---------|------|-------------|--------|
| **ESC-50** | 2,000 clips (40 per class) | 50 environmental sound classes including `crying_baby` | [GitHub](https://github.com/karolpiczak/ESC-50) — free |
| **AudioSet** (Google) | 2M+ clips | `Baby cry, infant cry` label (~15K clips) | Free download |
| **Donate a Cry Corpus** | ~1,000 clips | Crowd-sourced baby cry recordings | Free |
| **Baby Chillanto (DB1)** | ~2,000 clips | Mexican research dataset, categorized by cause | Academic |
| **UrbanSound8K** | 8,732 clips | 10 environmental classes (good for negatives) | Free |

### Negative Examples (Non-Cry Sounds)

Critical for false-positive reduction. Include:
- Adult speech, shouting
- TV/radio audio (especially scenes with babies)
- Pet sounds (cats meowing, dogs whining)
- Household alarms, doorbell
- Music (especially high-pitched)
- HVAC, fan, traffic noise

### Synthetic Data Generation

Recent research (2023-2025) shows **synthetic cry data** generated via text-to-audio models (AudioLDM, Bark) can supplement real recordings:
- Generate "baby crying in a quiet room" / "baby crying with TV in background"
- Augments limited real datasets
- Helps with data diversity

### Recommended Training Approach

1. **ESC-50** crying_baby class (40 clips) + **Donate a Cry** (~1,000 clips) = ~1,040 crying samples
2. **ESC-50** other 49 classes (1,960 clips) + **UrbanSound8K** subset = ~3,000+ non-crying samples
3. **Data augmentation:** Time-shift, pitch-shift (±10%), add background noise, gain variation
4. **Train/validation/test split:** 70/15/15
5. **Target: ≥2,000 crying + ≥2,000 non-crying** after augmentation

---

## 9. Recommendation

### Recommended: Architecture C (Hybrid VAD + CNN)

**Why:**
- Best power efficiency (critical for battery-powered device)
- 92-96% accuracy is production-quality
- Comfortable RAM fit with display + WiFi
- Well-supported toolchain

### Recommended ML Framework: Edge Impulse (train) + TFLite Micro (deploy)

- Use Edge Impulse studio to collect data, train, and iterate quickly
- Export TFLite INT8 model
- Deploy via `esp-tflite-micro` ESP-IDF component with custom feature pipeline

### Recommended Model Spec

| Parameter | Value |
|-----------|-------|
| Sample rate | 16 kHz |
| Frame / hop | 512 / 256 samples (32ms / 16ms) |
| FFT size | 512 |
| Features | 40 mel bands |
| Analysis window | 1.5 seconds (~94 frames) |
| Model | 2-3 layer CNN, depthwise-separable convolutions |
| Quantization | INT8 (full integer) |
| Model flash | ~30-60 KB |
| Inference RAM | ~40-60 KB |
| Inference time | ~50-100 ms every 1.5s |
| Expected accuracy | 92-96% |
| VAD pre-filter | Energy threshold on raw audio |

### System Architecture

```
┌─────────────────────────────────────────────────────┐
│  ESP32-C6-Touch-AMOLED-1.8                          │
│                                                     │
│  [PDM Mic] → [ES8311] → [I2S DMA]                  │
│                              │                      │
│                     [Energy VAD]                     │
│                         │    │                       │
│                    quiet│    │loud                   │
│                         │    │                       │
│                    [sleep]   [Mel Spectrogram]       │
│                              │                      │
│                         [TFLite CNN]                 │
│                          │       │                   │
│                     crying    not crying              │
│                       │          │                   │
│               ┌───────┘          └──[continue]       │
│               │                                     │
│        [Update LVGL UI]  ←  Status screen           │
│        [WiFi HTTP POST]  →  Phone notification      │
│        [Speaker alert]   →  Optional local alarm    │
│                                                     │
│  [AXP2101]  Battery monitor, long-press power off   │
│  [SH8601]   AMOLED status display (always on)       │
└─────────────────────────────────────────────────────┘
```

### Display UI Concept

```
┌──────────────────┐
│   Baby Monitor   │
│                  │
│   ○ Listening    │  ← Green dot: normal monitoring
│                  │  ← Red dot + "CRYING DETECTED" on alert
│   🔋 85% ⚡      │  ← Battery from AXP2101
│                  │
│   WiFi: ✓        │
│   192.168.1.100  │
│                  │
│   Last cry:      │
│   10:23 PM       │  ← Timestamp from PCF85063 RTC
│   Duration: 45s  │
│                  │
│   Today: 3 cries │  ← Statistics
│   Total: 12 min  │
└──────────────────┘
```

### Development Phases

| Phase | Description | Deliverable |
|-------|-------------|-------------|
| **1. Audio Pipeline** | ES8311 init, I2S capture, verify audio stream | Raw PCM over serial / WAV file |
| **2. Feature Extraction** | FFT → mel spectrogram, streaming mode | Spectrogram visualization |
| **3. DSP Prototype** | Energy VAD + frequency band detector | Basic cry/no-cry with ~80% accuracy |
| **4. ML Training** | Collect dataset, train CNN, export TFLite INT8 | Quantized .tflite model file |
| **5. ML Integration** | Deploy TFLite Micro, run inference on-device | On-device classification |
| **6. Display + WiFi** | LVGL status UI, WiFi notification push | Complete baby monitor |
| **7. Power Management** | VAD-gated inference, display dim, deep sleep options | Battery-optimized build |

### Key Technical Risks

| Risk | Mitigation |
|------|-----------|
| **TFLite Micro on RISC-V perf** | Use INT8 quantization, small model. Fallback: classical ML (SVM). |
| **ES8311 DMIC quality** | ALC + HPF enabled. Test with real baby audio early. |
| **False positives (TV, pets)** | Temporal smoothing (require 3+ consecutive detections). Include adversarial examples in training. |
| **WiFi + audio contention** | I2S uses DMA (CPU-free). WiFi sends are brief bursts. Use FreeRTOS task priorities. |
| **RAM pressure** | INT8 mel spectrogram (4 KB vs 16 KB float). Reduce LVGL heap to 24-32 KB. |

---

## Sources

- [ESP-SR GitHub — WakeNet9s C6 support](https://github.com/espressif/esp-sr)
- [esp-tflite-micro ESP Component](https://components.espressif.com/components/espressif/esp-tflite-micro)
- [ESP-DSP Library](https://github.com/espressif/esp-dsp)
- [TinyML Prototype of Infant Cry Classification System (IEEE 2025)](https://ieeexplore.ieee.org/document/11132295/)
- [Baby Cry Detection with TinyML and Synthetic Data (Arduino Blog)](https://blog.arduino.cc/2023/04/24/detect-a-crying-baby-with-tinyml-and-synthetic-data/)
- [Hello Edge: Keyword Spotting on Microcontrollers (ARM Research 2018)](https://arxiv.org/abs/1711.07128)
- [MCUNet: Tiny Deep Learning on IoT Devices (MIT 2020)](https://arxiv.org/abs/2007.10319)
- [TinyML Baby Cry Detection on Hackster.io](https://www.hackster.io/shahizat/tinyml-baby-cry-detection-using-chatgpt-and-synthetic-data-1e715b)
- [Edge Impulse ESP32 Audio Classification](https://docs.edgeimpulse.com/hardware/boards/espressif-esp32)
- [ESC-50 Dataset](https://github.com/karolpiczak/ESC-50)
- [Donate a Cry Corpus](https://github.com/gveres/donateacry-corpus)
- [ESP-SR WakeNet Documentation](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/wake_word_engine/README.html)
- [ES8311 Datasheet](https://files.waveshare.com/wiki/common/ES8311.DS.pdf)
- [ESP32-C6 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)
