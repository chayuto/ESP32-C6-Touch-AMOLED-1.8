# 12_baby_cry_dsp — Baby Cry Detection (Pure DSP)

Baby cry detection using pure DSP on the ESP32-C6-Touch-AMOLED-1.8 board. No ML model
required — detects crying through FFT frequency analysis, harmonic verification,
gated multi-feature scoring, and temporal pattern recognition.

For the full research alignment and academic citations, see
[DETECTION_METHODOLOGY.md](DETECTION_METHODOLOGY.md).

## How It Works

```
[PDM Mic] -> [ES8311 ADC] -> [I2S DMA] -> [Ring Buffer]
                                                |
                                    [Energy VAD (adaptive)]
                                         |         |
                                    silence     audio active
                                         |         |
                                      [skip]   [512-pt FFT]
                                                   |
                                        [Hard Gates]
                                        RMS / cry_ratio / low_ratio
                                         |         |
                                       fail      pass
                                     (score=0)     |
                                         [Gated Multi-Feature Scoring]
                                         cry band + harmonics + F0 +
                                         formants + crest + low reject +
                                         periodicity → score 0-100
                                                   |
                                       [State Machine + Alert]
                                       (trigger >= 65, clear < 45)
                                                   |
                           [AMOLED Display] + [SPIFFS Log] + [Serial Log]
```

## Features

| Feature | Description |
|---------|-------------|
| **FFT Spectrum** | Real-time 32-bar spectrum visualization on AMOLED, cry band highlighted |
| **Adaptive Detection** | Noise floor auto-adjusts to ambient level, 2x sensitivity over static threshold |
| **NTP Time Sync** | Opportunistic SNTP sync (pool.ntp.org), timestamps for cry events |
| **Data Logging** | Continuous SPIFFS logging (27-col CSV), SD card export at boot |
| **Battery Monitoring** | AXP2101 voltage, percentage, charge state on display |
| **Brightness Control** | BOOT button (GPIO 9) cycles 4 levels: dim(1) -> low(40) -> medium(120) -> bright(220) |
| **Power Management** | Long-press PWR button (2.5s) for hardware shutdown via AXP2101 |
| **Build Version** | Compile timestamp shown on display for firmware identification |

## Detection Algorithm (v3)

1. **Adaptive Energy VAD** — EMA noise floor, analyzes when RMS exceeds `noise_floor x 1.5`
2. **512-point FFT** — Hanning-windowed, 50% overlap (32 ms frames, 16 ms hop)
3. **Hard Gates** — three mandatory preconditions block scoring if not met:
   - RMS > noise_floor * 2.5 (reject near-silence)
   - cry_ratio > 0.08 (reject sounds with no cry-band energy)
   - low_ratio < 0.35 (reject bass-heavy adult speech/music)
4. **Multi-Feature Scoring** (0-100 weighted sum):
   - Cry band energy ratio (350-550 Hz) — 30 pts max
   - F0 + harmonic verification + cry dominance — 20 pts
   - Formant energy (1-3.5 kHz) — 10 pts
   - Spectral crest (tonality) — 5 pts
   - Low-frequency rejection (<250 Hz) — 20 pts
   - Cry-pause periodicity — 10 pts
5. **Temporal Smoothing** — 4 consecutive positive blocks to trigger, 4 to clear, with hysteresis (trigger >= 65, clear < 45)

See [DETECTION_METHODOLOGY.md](DETECTION_METHODOLOGY.md) for research citations
and detailed rationale behind each feature.

## Display Layout

```
+------------------------------------+
|        Baby Monitor (DSP)          |  <- Title (20px)
|                                    |
|          Listening...              |  <- Status (36px, green/red)
|                                    |
| Level: 42  RMS: 312  NF: 180      |  <- Audio metrics
| [====================............] |  <- RMS level bar
|                                    |
| FFT Spectrum (0-8kHz)              |
| ||| |||||||||||| ||| || | | | |    |  <- 32 spectrum bars
| ^^^ cry band ^^^                   |     (cry band = orange)
|                                    |
| Cry: 18%  Period: 4  Thr: 360     |  <- Detection detail
| ---------------------------------- |
| Batt: 85% 4012mV CHG  WiFi: .100  |  <- System status grid
| NTP: 14:32:05          SD: OK     |
|                                    |
| Events: 3  Last: 2m15s ago (14:30)|  <- Cry statistics
| BTN: 5  Bright: 120  GPIO9: 1     |  <- Button debug
| Build: Apr  4 2026 18:45:22       |  <- Firmware version
+------------------------------------+
```

## Hardware Used

| Peripheral | Role |
|-----------|------|
| ES8311 Codec | Mono ADC, 16kHz/16-bit, I2C 0x30 (8-bit addr), I2S data |
| Dual PDM Mics | Audio capture (one mic through ES8311) |
| SH8601 AMOLED | 368x448 display, FFT spectrum + status UI |
| FT3168 Touch | Capacitive touch (I2C 0x38) |
| AXP2101 PMIC | Battery monitoring, long-press power off |
| TCA9554 IO Exp | Display power (P4), touch power (P5) |
| BOOT Button | GPIO 9, brightness cycle (4 levels) |
| SD Card Slot | SPI (CLK=11, CMD=10, DATA=18, CS=6), cry event logging |
| WiFi 6 | NTP time sync |

## Build & Flash

```bash
# Activate ESP-IDF
. ~/esp/esp-idf/export.sh

# One-time: copy WiFi credentials
cp sdkconfig.defaults.template sdkconfig.defaults
# Edit: set CONFIG_CANVAS_WIFI_SSID and CONFIG_CANVAS_WIFI_PASSWORD

# One-time: set target
idf.py set-target esp32c6

# Build & flash
idf.py build
idf.py -p /dev/cu.usbmodem3101 flash

# Monitor
idf.py -p /dev/cu.usbmodem3101 monitor
```

If flash fails (board unresponsive): hold BOOT button, press PWR button, release BOOT, then flash.

## Project Structure

```
DETECTION_METHODOLOGY.md  — Research alignment, academic citations, design rationale
main/
  main.c              — Startup, WiFi, LVGL task, button handler
  audio_capture.c/h   — ES8311 via esp_codec_dev, I2S 16kHz, ring buffer
  cry_detector.c/h    — FFT, gated multi-feature scoring, harmonic verification
  ui_monitor.c/h      — LVGL UI with spectrum bars, status grid, build version
  ntp_time.c/h        — Opportunistic SNTP sync (AEST timezone)
  sd_logger.c/h       — SPIFFS logging + SD card export (27-column CSV)
  Kconfig.projbuild   — WiFi config
  idf_component.yml   — Dependencies
```

## Dependencies

- `espressif/esp_codec_dev` ^1.4.0 — ES8311 codec driver
- `espressif/esp-dsp` >=1.0.0 — DSP library (FFT reference available)
- Shared `amoled_driver` — Display, touch, PMIC

## RAM Budget

```
LVGL draw buffers:  65 KB  (2x 368x45 RGB565)
LVGL heap:          32 KB
WiFi:               55 KB
I2S DMA:             4 KB
Audio ring buffer:  16 KB
FFT workspace:      12 KB  (input + real + imag + magnitude + window)
Spectrum UI:         1 KB
Detection state:     2 KB
FreeRTOS tasks:     24 KB  (LVGL 8K + audio 4K + cry_det 8K + NTP 4K)
ES8311 driver:       2 KB
SD/FAT:              8 KB  (when mounted)
Misc:               10 KB
────────────────────────────
Total:             ~231 KB of 452 KB
Free:              ~221 KB
```

## Key Technical Findings

- **ES8311 I2C address**: `esp_codec_dev` expects 8-bit format (`0x30`), not 7-bit (`0x18`)
- **SH8601 QSPI brightness**: Command must be encoded as `(0x02 << 24) | (0x51 << 8)`, not raw `0x51`
- **ESP-SR VADNet**: Not available standalone on ESP32-C6 (bundled in AFE, ESP32/S3/P4 only)
- **GPIO 9 BOOT button**: Safe to use as user button after boot (strapping pin, pull-up, active LOW)
- **No FPU on RISC-V C6**: Floating-point FFT works but is software-emulated. INT8 quantization recommended for future ML models.
