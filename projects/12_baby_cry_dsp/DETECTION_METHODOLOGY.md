# Detection Methodology & Research Alignment

Baby cry detection on a resource-constrained microcontroller (ESP32-C6, RISC-V, no FPU)
using pure DSP — no machine learning model required. This document maps each detection
feature to the published research that supports it.

## Table of Contents

1. [System Overview](#system-overview)
2. [Acoustic Properties of Infant Cry](#acoustic-properties-of-infant-cry)
3. [Feature Extraction Pipeline](#feature-extraction-pipeline)
4. [Hard Gating (Pre-screening)](#hard-gating-pre-screening)
5. [Multi-Feature Scoring](#multi-feature-scoring)
6. [Temporal State Machine](#temporal-state-machine)
7. [Design Rationale: Why Not ML?](#design-rationale-why-not-ml)
8. [References](#references)

---

## System Overview

```
PDM Mic → ES8311 ADC → I2S DMA (16 kHz, 16-bit mono)
                            ↓
                    1.5 s analysis window
                            ↓
              512-pt FFT, Hanning window, 50% overlap
                            ↓
                 ┌──────────────────────┐
                 │     HARD GATES       │
                 │  RMS > noise × 2.5   │
                 │  cry_ratio > 0.08    │
                 │  low_ratio < 0.35    │
                 └─────────┬────────────┘
                      pass │ fail → score = 0
                           ↓
              ┌─────────────────────────┐
              │  MULTI-FEATURE SCORING  │
              │  (0-100 weighted sum)   │
              └─────────┬───────────────┘
                        ↓
              State machine (4 confirms / 4 clears)
                        ↓
              Alert + CSV log + display update
```

**Implementation:** `cry_detector.c` (v3, ~600 lines C)

---

## Acoustic Properties of Infant Cry

The detection approach is built on well-established acoustic properties of infant crying,
studied since the 1960s.

### Fundamental Frequency (F0): 350-600 Hz

The infant cry fundamental frequency is one of the most replicated findings in
developmental acoustics. Healthy newborns produce cries with F0 typically between
350-600 Hz, with a mean around 450-500 Hz.

Michelsson et al. [1] measured 172 healthy newborns and found mean F0 of 496 +/- 95 Hz,
with 93% of infants having mean F0 below 600 Hz. This builds on the foundational
spectrographic work of Wasz-Hockert et al. [2], who established the methodology for
infant cry analysis in the 1960s. Earlier work by Michelsson [3] defined the
classification still used today: basic phonation (F0 350-750 Hz), hyperphonation
(F0 1000-2000 Hz), and dysphonation (aperiodic noise).

**Our implementation:** Cry band defined as bins 11-17 of a 512-point FFT at 16 kHz
sample rate, corresponding to 344-531 Hz. This was narrowed from an initial 250-600 Hz
band (bins 8-19) after real-world testing showed the wider band captured too much adult
female speech (F0 165-255 Hz, with 2nd harmonic at 330-510 Hz overlapping the old range).

### Harmonic Structure

Baby cries exhibit a strong harmonic ladder — energy at integer multiples of F0 extending
up to 3-5 kHz. This is a consequence of the vocal fold vibration mechanism in infant
phonation, modeled by Golub and Corwin [4] in their physioacoustic model of the infant
cry. Their model describes four acoustic categories of infant vocalization, with the
dominant mode (expiratory phonation) producing quasi-periodic signals rich in harmonics.

Adult speech also has harmonics, but they are weaker relative to the fundamental and less
regular. More importantly, in adult speech the fundamental sits below 250 Hz, so the
harmonic ladder starts at a different place in the spectrum.

**Our implementation:** For each FFT frame, we identify the peak bin in the cry band as a
candidate F0. We then check for energy at 2*F0 (>= 35% of F0 magnitude) and 3*F0
(>= 20% of F0 magnitude), with +/-1 bin tolerance for spectral leakage. Crucially, we
require **cry band dominance** — the cry band must have more energy than the sub-bass
band (<250 Hz) — before counting harmonics. This prevents adult speech (strong F0 below
250 Hz with weaker harmonics above) from triggering the harmonic detector.

This approach is directly inspired by Torres et al. [5], who introduced Harmonic Ratio
Accumulation (HRA) as a hand-crafted feature for baby cry detection. Their work showed
that tracking harmonics of the cry fundamental, combined with voiced/unvoiced segment
counting, achieves classification accuracy comparable to CNN-based approaches at a
fraction of the computational cost — critical for microcontroller deployment.

### Cry-Pause Temporal Pattern

Infant crying follows a characteristic rhythmic pattern: an expiratory phonation burst
(the audible cry) followed by a brief inspiratory pause, repeating at approximately
1 Hz. This temporal signature distinguishes crying from continuous sounds like music,
machine noise, or sustained speech.

The physiological basis is the infant breathing cycle during crying. Golub and Corwin [4]
describe the expiratory-inspiratory cycle as fundamental to cry production. The specific
timing (~0.7-1.3 second total cycle) varies with cry type and infant age but is
consistent enough to serve as a detection feature.

**Our implementation:** We maintain a 20-sample energy history and count transitions above
and below the mean energy level (a simple zero-crossing detector on the energy envelope).
A transition count >= 5 indicates cry-pause periodicity. This was raised from 3 after
v2 data showed that household sounds (TV, conversation) regularly produced 3-4
transitions.

### Spectral Energy Distribution

Baby cries concentrate energy in specific frequency bands that differ from adult speech:

- **Sub-bass (<250 Hz):** Minimal in infant cries; dominant in adult male speech
  and significant in adult female speech
- **Cry band (350-550 Hz):** Primary F0 region for infant cries
- **Formant region (1-3.5 kHz):** Baby cry formants F1 (~1100 Hz) and F2 (~2600 Hz)
  produce significant energy here [6]

LaGasse, Neal, and Lester [6] provide a comprehensive review of infant cry acoustic
features, confirming that the spectral shape of infant crying — with its characteristic
F0, formant frequencies, and harmonic pattern — is sufficiently distinct from other
sound sources to enable automated detection.

**Our implementation:** We compute three band energy ratios:
- `cry_ratio` = energy in 344-531 Hz / total energy
- `low_ratio` = energy in 0-219 Hz / total energy  
- `high_ratio` = energy in 1000-3500 Hz / total energy

These ratios, rather than absolute magnitudes, provide robustness to varying recording
levels and distances.

---

## Feature Extraction Pipeline

### FFT Configuration

- **512-point FFT**, Hanning window, 50% overlap (32 ms frames, 16 ms hop)
- **16 kHz sample rate** — Nyquist at 8 kHz, sufficient for cry analysis up to ~4 kHz
- **Frequency resolution:** 31.25 Hz per bin (16000 / 512)
- **1.5-second analysis window** — approximately 1.5 cry-pause cycles, yielding ~11 frames

This configuration follows standard practices in audio analysis. The 32 ms frame size
is within the 20-40 ms range commonly used for speech and cry analysis where the signal
can be considered quasi-stationary [7].

### Spectral Crest Factor

The spectral crest factor (ratio of peak magnitude to mean magnitude across the spectrum)
measures tonality. Tonal signals like cries have high crest; broadband noise has low
crest. Defined by Peeters [8] as a standard audio descriptor and formalized in the
MPEG-7 Audio standard (ISO/IEC 15938) as the Audio Spectrum Flatness measure [9].

**Our implementation:** `max_magnitude / mean_magnitude` across all FFT bins. Threshold
set to 6.0 (raised from 4.0 in v2 after data showed even broadband household sounds
exceeded 4.0).

### Adaptive Noise Floor

We use an exponential moving average (EMA) on RMS energy during silence to track the
ambient noise level. The noise floor adapts at 5% per update (`NOISE_ADAPT_RATE = 0.05`),
balancing responsiveness with stability. This approach is standard in Voice Activity
Detection systems [10][11].

---

## Hard Gating (Pre-screening)

A key architectural decision in v3: three mandatory preconditions must ALL pass before
any scoring occurs. If any gate fails, the frame scores 0 regardless of other features.

| Gate | Condition | Rejects |
|------|-----------|---------|
| RMS gate | `RMS > noise_floor * 2.5` | Near-silence, distant sounds |
| Cry ratio gate | `cry_ratio > 0.08` | Sounds with no energy in cry band |
| Low ratio gate | `low_ratio < 0.35` | Bass-heavy sounds (adult speech, music) |

**Why gating?** In v2, features were scored independently without prerequisites. Data
showed that features like `harmonic_pct` (69-96% on ALL sounds) and `crest` (always > 4.0)
were non-discriminating — they passed for nearly every input. Without gates, scores
accumulated to 50+ even on silence, making the clear threshold of 25 unreachable.

This gating approach mirrors standard practice in Voice Activity Detection (VAD) systems,
where energy-based pre-screening eliminates obvious non-speech before applying more
expensive feature analysis [10][11]. Sohn et al. [10] established the statistical
model-based VAD framework where likelihood ratio tests serve as gates before detailed
analysis — our hard gates serve the same architectural role.

The `low_ratio` gate specifically targets the adult speech false positive problem:
adult speech has significantly more energy below 250 Hz relative to total energy than
infant crying does. This exploits the well-documented F0 difference between infants
(350-600 Hz) and adults (85-255 Hz) [1][6].

---

## Multi-Feature Scoring

After passing all gates, six features contribute to a weighted score (0-100):

| Feature | Points | Threshold | Research basis |
|---------|--------|-----------|----------------|
| Cry band energy | 20+10 | ratio > 0.12 / 0.25 | F0 range [1][2][3] |
| F0 + harmonics + dominance | 20 | Verified H2+H3 + cry dominant | HRA method [5], physioacoustic model [4] |
| F0 only (no harmonics) | 5 | F0 in range + cry dominant | Partial match |
| High-band formants | 10 | ratio > 0.08 | Cry formants F1/F2 [6] |
| Spectral crest | 5 | crest > 6.0 | Tonality measure [8][9] |
| Low-frequency rejection | 20 | low_ratio < 0.12 | Adult speech discrimination [1][6] |
| Periodicity | 10 | transitions >= 5 | Cry-pause pattern [4] |

**Trigger threshold: 65.** Requires at minimum: cry band present (20) + harmonics verified
(20) + low rejection (20) + one more feature. This ensures no single feature can trigger
detection alone.

**Clear threshold: 45.** Hysteresis prevents rapid state toggling. Once crying is detected,
the score must drop below 45 (not just below 65) to clear.

**Weight rationale:** Features are weighted inversely to their false positive rate in v2
field data. `cry_ratio` and `low_ratio` were the most discriminating features in household
testing (TV, conversation, kitchen sounds, music). `crest` and `periodicity` were the
least discriminating, so they receive lower weights.

This multi-feature scoring approach follows the hand-crafted feature philosophy validated
by Torres et al. [5], who demonstrated that carefully chosen acoustic features can match
deep learning performance for baby cry detection, achieving comparable accuracy to their
CNN baseline while requiring 20x less computation.

---

## Temporal State Machine

Detection requires temporal consistency to reject transient sounds:

- **4 consecutive positive blocks** to trigger (4 * 1.5s = 6 seconds)
- **4 consecutive negative blocks** to clear
- Hysteresis via different trigger (65) and clear (45) thresholds

This temporal smoothing is standard in audio event detection. The specific confirmation
count was tuned empirically: 2 confirms (v1) triggered on brief loud sounds; 4 confirms
requires sustained crying consistent with the cry-pause pattern documented in the
literature [4].

---

## Design Rationale: Why Not ML?

The ESP32-C6 (RISC-V, 160 MHz, no FPU, no PSRAM, ~220 KB free RAM) imposes hard
constraints that favor pure DSP:

1. **No floating-point hardware.** All `float` operations are software-emulated. A CNN
   inference pass would consume significant CPU time. INT8 quantized TFLite Micro is
   possible [12] but adds complexity and flash usage.

2. **Limited RAM.** After LVGL display buffers, WiFi stack, and I2S DMA, only ~220 KB
   remains. CNN models for audio classification typically require 50-500 KB for weights
   alone.

3. **Real-time requirement.** The detector must process 1.5-second blocks continuously
   while sharing CPU with display rendering, WiFi, and logging — all on a single core.

4. **Validated precedent.** Torres et al. [5] demonstrated that hand-crafted features
   (Harmonic Ratio Accumulation, voiced segment counting, F0 tracking) achieve accuracy
   comparable to CNN approaches for baby cry detection. Their HCBC features achieved
   this at 20x lower computational cost — and our target platform is even more
   constrained than their test environment.

5. **Interpretability.** Every detection decision can be traced through the score
   breakdown (cry_ratio, harmonics, low_ratio, etc.), logged to CSV, and tuned. A neural
   network would be a black box on-device.

Ji et al. [12] provide a comprehensive review of infant cry classification methods,
confirming that while deep learning approaches achieve slightly higher accuracy on
benchmark datasets, spectral feature-based methods remain competitive and are more
suitable for deployment on resource-constrained edge devices.

---

## References

[1] Michelsson, K., Eklund, K., Leppanen, P., & Lyytinen, H. (2002). "Cry
characteristics of 172 healthy 1- to 7-day-old infants." *Folia Phoniatrica et
Logopaedica*, 54(4), 190-200. doi:10.1159/000063190

[2] Wasz-Hockert, O., Lind, J., Vuorenkoski, V., Partanen, T., & Valanne, E. (1968).
*The Infant Cry: A Spectrographic and Auditory Analysis.* Clinics in Developmental
Medicine, No. 29. London: William Heinemann Medical Books.

[3] Michelsson, K. (1971). "Cry analyses of symptomless low birth weight neonates and
of asphyxiated newborn infants." *Acta Paediatrica Scandinavica*, 60(Suppl. 216), 1-45.
doi:10.1111/j.1651-2227.1971.tb05679.x

[4] Golub, H.L. & Corwin, M.J. (1985). "A physioacoustic model of the infant cry."
In B.M. Lester & C.F.Z. Boukydis (Eds.), *Infant Crying: Theoretical and Research
Perspectives* (pp. 59-82). New York: Plenum Press. doi:10.1007/978-1-4613-2381-5_3

[5] Torres, R., Battaglino, D., & Lepauloux, L. (2017). "Baby cry sound detection:
A comparison of hand crafted features and deep learning approach." In *Proc. 18th
International Conference on Engineering Applications of Neural Networks (EANN 2017)*,
Communications in Computer and Information Science, Vol. 744 (pp. 168-179). Cham:
Springer. doi:10.1007/978-3-319-65172-9_15

[6] LaGasse, L.L., Neal, A.R., & Lester, B.M. (2005). "Assessment of infant cry:
Acoustic cry analysis and parental perception." *Mental Retardation and Developmental
Disabilities Research Reviews*, 11(1), 83-93. doi:10.1002/mrdd.20050

[7] Lavner, Y., Cohen, R., Ruinskiy, D., & IJzerman, H. (2016). "Baby cry detection
in domestic environment using deep learning." In *Proc. IEEE International Conference
on the Science of Electrical Engineering (ICSEE)*. See also: Cohen, R., Ruinskiy, D.,
Zickfeld, J., IJzerman, H., & Lavner, Y. (2020). "Baby cry detection: Deep learning
and classical approaches." In W. Pedrycz & S.M. Chen (Eds.), *Development and Analysis
of Deep Learning Architectures* (pp. 171-196). Springer. doi:10.1007/978-3-030-31764-5_7

[8] Peeters, G. (2004). "A large set of audio features for sound description (similarity
and classification) in the CUIDADO project." IRCAM Technical Report. See also: Peeters,
G., Giordano, B.L., Susini, P., Misdariis, N., & McAdams, S. (2011). "The Timbre
Toolbox: Extracting audio descriptors from musical signals." *Journal of the Acoustical
Society of America*, 130(5), 2902-2916. doi:10.1121/1.3642604

[9] ISO/IEC 15938 (2002). *Multimedia Content Description Interface (MPEG-7).* Audio
Spectrum Flatness and Spectral Crest definitions.

[10] Sohn, J., Kim, N.S., & Sung, W. (1999). "A statistical model-based voice activity
detection." *IEEE Signal Processing Letters*, 6(1), 1-3. doi:10.1109/97.736233

[11] Ramirez, J., Gorriz, J.M., & Segura, J.C. (2007). "Voice activity detection:
Fundamentals and speech recognition system robustness." In M. Grimm & K. Kroschel
(Eds.), *Robust Speech Recognition and Understanding.* IntechOpen.
doi:10.5772/4740

[12] Ji, C., Mudiyanselage, T.B., Gao, Y., & Pan, Y. (2021). "A review of infant cry
analysis and classification." *EURASIP Journal on Audio, Speech, and Music Processing*,
2021, Article 8. doi:10.1186/s13636-021-00197-5
