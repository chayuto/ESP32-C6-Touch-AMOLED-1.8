# 13_baby_cry_vad -- Baby Cry Detection with VAD + DSP

Baby cry detection for ESP32-C6-Touch-AMOLED-1.8 using a two-stage pipeline: Voice Activity Detection (VAD) gate followed by FFT frequency analysis.

## How It Works

### Two-Stage Detection Pipeline

1. **Stage 1: VAD Gate** -- Filters out non-vocal noise before DSP analysis
2. **Stage 2: FFT Analysis** -- When VAD detects voice activity, runs 512-point FFT to check for baby cry frequency signature (F0: 250-600 Hz)

### Detection Logic

- VAD must detect voice activity before FFT analysis runs (saves CPU, reduces false positives)
- Cry band ratio: energy in 250-600 Hz / total spectral energy
- Threshold: cry_band_ratio > 25%
- Temporal smoothing: 3 consecutive positive frames to trigger alert, 3 negative to clear

## ESP-SR VADNet Integration Research

### What Was Attempted

ESP-SR's VADNet was the original target for Stage 1 voice activity detection. Research findings:

1. **ESP-SR v2.x Component Registry** (`espressif/esp-sr`): Supports ESP32-C6 but **only for WakeNet** (wake word detection). WakeNet9 and WakeNet9s can run on C6.

2. **VADNet**: Bundled inside the Audio Front-End (AFE) pipeline, which **only supports ESP32, ESP32-S3, and ESP32-P4**. There is no standalone VADNet API.

3. **Older WebRTC VAD** (`esp_vad.h` from esp-adf): Uses `vad_create(VAD_MODE_x)` / `vad_process()` API. This is part of esp-sr/esp-adf and may have pre-built binaries for C6, but the component pulls in the entire esp-sr framework which may cause build issues on C6.

4. **Hardware VAD**: ESP32-P4 has dedicated hardware VAD peripheral. ESP32-C6 does not.

### Decision: Energy-Based VAD

Since VADNet is not available standalone on ESP32-C6, and pulling in the full esp-sr component risks build failures and excessive memory usage, an energy-based VAD was implemented instead:

- Computes short-term energy of each 30ms audio frame
- Maintains adaptive noise floor estimate
- Speech detected when frame energy exceeds noise_floor x 4.0
- Hangover counter prevents rapid on/off switching (6 frames = 180ms)
- Tuned for baby cry detection (higher energy than typical adult speech)

This approach is sufficient for the baby monitoring use case since baby crying is a high-energy signal that is easily distinguished from background noise by energy alone.

## Hardware

- **Board**: Waveshare ESP32-C6-Touch-AMOLED-1.8
- **Audio**: ES8311 codec (I2C 0x18 control, I2S data), 16kHz/16-bit mono
- **Display**: SH8601 AMOLED, 368x448, QSPI
- **PMIC**: AXP2101 (battery monitoring, power off)
- **Touch**: FT3168 (I2C 0x38)

## Build Instructions

```bash
# Activate ESP-IDF
. ~/esp/esp-idf/export.sh

# Set target (first time only)
idf.py -C projects/13_baby_cry_vad set-target esp32c6

# Copy sdkconfig template and set WiFi credentials
cp projects/13_baby_cry_vad/sdkconfig.defaults.template projects/13_baby_cry_vad/sdkconfig.defaults
# Edit sdkconfig.defaults: set BABY_CRY_WIFI_SSID and BABY_CRY_WIFI_PASSWORD

# Build
idf.py -C projects/13_baby_cry_vad build

# Flash
idf.py -C projects/13_baby_cry_vad -p /dev/cu.usbmodem1101 flash
```

## Project Structure

```
main/
  main.c              -- Startup, WiFi, LVGL task
  audio_capture.c/h   -- ES8311 + I2S, ring buffer
  vad_filter.c/h      -- Energy-based VAD (ESP-SR fallback)
  cry_detector.c/h    -- FFT analysis + temporal smoothing
  ui_monitor.c/h      -- LVGL baby monitor UI
  Kconfig.projbuild   -- WiFi SSID/password config
  idf_component.yml   -- Dependencies
```

## Dependencies

- `espressif/esp_codec_dev` ^1.4.0 -- ES8311 codec driver
- `espressif/esp_dsp` -- FFT for frequency analysis
- Shared `amoled_driver` component -- Display, touch, PMIC

## UI Display

- Title: "Baby Monitor (VAD)"
- Large status: "Listening..." (green) / "Voice Detected" (yellow) / "CRYING!" (red)
- VAD state indicator
- Audio level bar (color-coded)
- Detection stats (event count, band ratio, RMS)
- Battery info (voltage, percentage, charging state)
- WiFi status (IP address)

## RAM Budget

```
LVGL draw buffers: ~65 KB
LVGL heap:          32 KB
WiFi:               55 KB
I2S DMA:             4 KB
Audio ring buffer:   8 KB
FFT workspace:       6 KB  (512*2 floats + window)
Detection state:     2 KB
FreeRTOS tasks:     24 KB
ES8311 driver:       2 KB
Misc:               10 KB
Total:             ~208 KB of 452 KB
```
