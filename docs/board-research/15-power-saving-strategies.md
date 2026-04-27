# Power Saving Strategies — ESP32-C6 Baby Cry Monitor

> Documented strategies for extending battery life on the Waveshare ESP32-C6-Touch-AMOLED-1.8 running the baby cry detection firmware (project 12_baby_cry_dsp).
> Date: 2026-04-04

---

## Power Budget (Baseline — Everything On)

| Component | Current Draw | Notes |
|-----------|-------------|-------|
| **WiFi (active)** | **80-100 mA** | Biggest drain. Always listening for packets. |
| ESP32-C6 CPU @ 160MHz | ~20 mA | Running FreeRTOS idle task, timers |
| SH8601 AMOLED (bright) | ~20-30 mA | Depends on content — OLED pixels self-emit |
| LVGL rendering | ~5 mA CPU overhead | Redraws dirty regions every 10ms |
| ES8311 codec | ~8 mA | ADC always sampling at 16kHz |
| I2S DMA | <1 mA | Hardware DMA, no CPU cost |
| FFT analysis | ~2 mA burst | 1-2ms every 1.5s = <0.1% duty cycle |
| FT3168 touch | ~1 mA | I2C polling |
| AXP2101 PMIC | ~1 mA | Always on (manages all power rails) |
| **Total (all on)** | **~140-170 mA** | **~350 mAh battery = ~2-2.5 hours** |

## Strategy 1: WiFi Off After NTP Sync (Implemented)

**Savings: ~80-100 mA (~60% of total power)**

WiFi is on for ~30 seconds at boot (connect + NTP sync), then turned off. Re-enabled for ~45 seconds every 6 hours to re-sync NTP.

| State | WiFi Duration | Current |
|-------|---------------|---------|
| Boot + NTP sync | ~30s | 150 mA |
| Normal operation | 5h 59m | ~60 mA (no WiFi) |
| NTP re-sync | ~45s | 150 mA |
| **Average over 6h** | | **~61 mA** |

**Timekeeping between syncs:** ESP32-C6 system clock uses the 40 MHz main crystal (~±20 ppm). Drift over 6 hours: ~0.4 seconds. Acceptable for timestamp logging.

**Implementation:** `ntp_time.c` — after initial SNTP sync, calls `esp_wifi_stop()`. A background task wakes WiFi every 6 hours via `esp_wifi_start()`, waits for SNTP callback, then stops WiFi again.

**Trade-off:** No network connectivity during WiFi-off periods. If future features need WiFi (push notifications, remote monitoring), this strategy needs adjustment — e.g., keep WiFi on but use modem sleep (`WIFI_PS_MIN_MODEM` saves ~30-50 mA).

## Strategy 2: Display Off via BOOT Button (Implemented)

**Savings: ~25-30 mA (display + LVGL rendering)**

BOOT button (GPIO 9) cycles 4 brightness levels: OFF → dim(40) → medium(120) → bright(220).

When OFF:
- SH8601 receives Display Off command (`0x28`) — AMOLED controller enters low-power mode, stops pixel scanning
- LVGL timer handler is skipped — no rendering overhead
- Button polling slows from 10ms to 50ms — minor CPU saving

| Display State | Savings | Notes |
|---------------|---------|-------|
| Off (0x28 cmd) | ~25-30 mA | AMOLED controller + pixels off |
| LVGL skip | ~3-5 mA | No rendering CPU overhead |
| Slow poll | ~1 mA | 50ms vs 10ms loop |

**Implementation:** `main.c` — `amoled_display_on_off(false)` sends QSPI command, sets `s_display_on = false`. LVGL task checks this flag and skips `lv_timer_handler()`.

## Strategy 3: FFT Duty Cycle (Already Efficient)

**No change needed — already <0.1% CPU duty.**

The FFT runs a 512-point analysis every 1.5 seconds. Each analysis takes ~1-2ms at 160MHz. This is negligible power.

| Analysis | Duration | Interval | Duty |
|----------|----------|----------|------|
| 512-pt FFT + mag | ~1 ms | 1500 ms | 0.07% |
| Full block analysis | ~2 ms | 1500 ms | 0.13% |

**No FPU on RISC-V C6** — float operations are software-emulated. But at <0.2% duty cycle, this doesn't matter.

## Strategy 4: Audio Capture (Always On — Required)

**Cannot be reduced — core function.**

ES8311 codec must run continuously at 16kHz to detect crying. The I2S DMA transfers audio without CPU intervention. The codec draws ~8 mA regardless of signal level.

**Possible future optimization:** Duty-cycle the codec — capture 3 seconds, sleep 2 seconds. But this risks missing the onset of a cry episode. Not recommended for a baby monitor.

## Combined Power Profile

| Mode | Components Active | Estimated Current | Battery Life (350 mAh) |
|------|------------------|-------------------|----------------------|
| **All on** (baseline) | WiFi + display + audio + LVGL | ~150 mA | ~2.3 hours |
| **WiFi off** | Display + audio + LVGL | ~60 mA | ~5.8 hours |
| **WiFi off + display off** | Audio only + CPU idle | ~35 mA | ~10 hours |
| **Deep sleep** (not monitoring) | RTC only | ~0.01 mA | ~4 years |

## Recommended Operating Modes

### Daytime (Active Monitoring)
- WiFi: OFF (re-sync every 6h)
- Display: Medium brightness (130)
- Audio: Always on
- SD logging: Every 5 min
- **~60 mA → ~5.8 hours**

### Nighttime (Baby Sleeping)
- WiFi: OFF
- Display: OFF (press BOOT to check)
- Audio: Always on
- SD logging: Every 5 min
- **~35 mA → ~10 hours**

### Not in Use
- Long-press PWR button (2.5s) → AXP2101 hardware shutdown
- All power rails cut, ~0 current
- Short-press PWR to restart

## Future Optimization Opportunities

| Optimization | Savings | Effort | Risk |
|-------------|---------|--------|------|
| **CPU frequency scaling** (160→80 MHz during idle) | ~10 mA | Low | FFT takes 2x longer but still fast enough |
| **WiFi modem sleep** instead of full off | ~30-50 mA vs 80-100 | Low | Enables push notifications |
| **Light sleep** between audio frames | ~5-10 mA | Medium | Need to ensure I2S DMA wakes CPU |
| **PCF85063 RTC alarm** for periodic wake from deep sleep | Huge for duty-cycled monitoring | High | Misses cries during sleep |
| **Codec duty cycling** (3s on, 2s off) | ~3 mA | Medium | Misses cry onset |
| **Disable touch controller** when not needed | ~1 mA | Low | Lose touch input |

## Key Hardware Facts

- **AXP2101 PMIC** manages all power rails. Long-press (2.5s) = hardware shutdown.
- **SH8601 AMOLED** is self-emitting — black pixels = zero light = lower power than white. A dark UI theme saves power.
- **No PSRAM** — can't buffer large amounts of audio for deferred processing.
- **40 MHz crystal** keeps system time when running — ±20 ppm drift (~1.7s/day). Good enough for 6-hour NTP re-sync intervals.
- **PCF85063 RTC** (I2C 0x51) has its own 32.768 kHz crystal. 220 nA standby. Could maintain time during deep sleep if needed.
- **350 mAh LiPo** — small battery, power optimization matters significantly.
- **WiFi 6** — slightly more efficient than WiFi 4/5 for short burst connections, but the dominant cost is the radio being on at all.

## Measurement Methodology

To verify actual power consumption, measure with:
1. **USB power meter** between charger and board (measures total including charge current — subtract charge current)
2. **AXP2101 battery registers** — read voltage at intervals, compute discharge rate: `mA = (V1-V2)/R_internal * time`
3. **SD card metrics log** — battery voltage + percentage every 5 minutes reveals discharge curve over time
