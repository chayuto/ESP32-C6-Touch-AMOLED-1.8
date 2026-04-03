# ESP32-C6 + AMOLED-1.8 Hardware Specs & Limitations

Reference for designing features on the ESP32-C6-Touch-AMOLED-1.8 board.
Use this before writing any code that touches peripherals, memory, or wireless.

---

## CPU

- **Core:** Single RISC-V at 160MHz
- **No FPU** — software float is ~10-20× slower than hardware. Audit all `float`/`double` usage.
- **No SIMD** — SIMD (PIE) exists only on ESP32-S3. Libraries claiming SIMD acceleration don't use it on C6.
- **Single core** — `xTaskCreatePinnedToCore()` compiles but is wrong. Use `xTaskCreate()`.

## Memory

- **SRAM:** 512KB total, ~400KB available after OS/ROM overhead
- **Flash:** 16MB external NOR-Flash
- **No PSRAM** — everything must fit in 512KB SRAM
- **Display buffer:** 368×112×2 × 2 = ~161KB (1/4 screen double-buffer, mandatory)
- **Full framebuffer:** 368×448×2 = 322KB — does NOT fit in RAM, use partial rendering
- **Heap fragmentation risk:** prefer static allocation for long-lived objects

## Hardware Accelerators — What Exists

| Accelerator | Details | mbedtls auto-uses? |
|---|---|---|
| AES | 128/256-bit, DMA-capable, XTS for flash | Yes |
| SHA | SHA-1/224/256/384/512, DMA-capable | Yes |
| RSA | Up to 3072-bit | Yes (TLS) |
| ECC | Elliptic curve, ECDSA | Yes |
| HMAC | Dedicated hardware HMAC | Manual (`esp_hmac.h`) |
| RNG | True hardware RNG | Automatic |
| Digital Signature | Hardware DS | Manual |

## Hardware Accelerators — What Does NOT Exist

| Feature | Reality |
|---|---|
| **Hardware JPEG** | P4-only. Use `espressif/esp_new_jpeg` (software) |
| **Hardware FPU** | Not present — software float only |
| **SIMD** | Not present — S3-only |
| **PSRAM** | Not supported on ESP32-C6 |
| **LCD parallel interface** | Not present — S3-only |
| **Hardware 2D DMA (PPA)** | Not present |

## Display (SH8601 via QSPI)

- **Type:** AMOLED — self-emitting, infinite contrast, true blacks
- **Resolution:** 368×448 portrait, RGB565 (16-bit)
- **Interface:** QSPI (4 data lines) at 40MHz on SPI2_HOST
- **Max throughput:** ~10 Mpixels/s theoretical, 15-30 fps practical with LVGL
- **Coordinate constraint:** All draw coordinates must be divisible by 2 → LVGL rounder callback
- **Brightness:** Register 0x51 (0-255), no backlight PWM GPIO
- **AOD (Always-On Display):** Supported by IC (registers 0x48/0x49)
- **Deep standby:** Register 0x4F — use before cutting power for deep sleep
- **DMA buffer:** 1/4 screen double buffer = 2 × (368×112×2) = 164,864 bytes
- **ESP-IDF component:** `espressif/esp_lcd_sh8601` v2.0.1

## Touch (FT3168 via I2C)

- **I2C address:** 0x38 (FT5x06 family compatible)
- **Interrupt:** GPIO 15, active low pulse on touch
- **Multi-touch:** 2 points max
- **Gestures:** Swipe L/R/U/D, double-click (register 0xD3)
- **Power modes:** Active, Monitor, Standby, Hibernate
- **ESP-IDF component:** `espressif/esp_lcd_touch_ft5x06`

## Power Management (AXP2101 via I2C 0x34)

- **Input:** USB 5V or 3.7V LiPo battery (MX1.25 connector)
- **Outputs:** 5 DCDCs + 4 ALDOs + 2 BLDOs + 2 DLDOs + CPUSLDO
  - ALDO/BLDO: 0.5-3.5V, 300mA each
  - DCDC: up to 2000mA each
- **Battery charger:** 25mA-1000mA configurable, 4.0-4.4V target
- **Fuel gauge:** Battery voltage + percentage estimation
- **Power key:** Configurable short/long press actions, IRQ
- **Deep sleep current:** 52µA at 3.7V, 392µA at 3.3V
- **Library:** `XPowersLib` by Lewis He (Arduino + ESP-IDF)

## IMU (QMI8658 via I2C 0x6B)

- **Accelerometer:** ±2/4/8/16g, up to 8000 Hz ODR, 200µg/√Hz noise
- **Gyroscope:** ±16-2048°/s, up to 8000 Hz ODR, 15 mdps/√Hz noise
- **Features:** FIFO (128 samples), tap detection, pedometer, wake-on-motion
- **Use cases:** Auto-rotate display, gesture control, wrist-raise detection
- **Library:** `SensorLib` v0.3.3 by Lewis He

## RTC (PCF85063 via I2C 0x51)

- **Alarm:** Single alarm (minute, hour, day, weekday match)
- **Timer:** Countdown with interrupt
- **Backup battery:** Pads on board for maintaining time during main power loss
- **Accuracy:** ±2 ppm typical at 25°C
- **Deep sleep wakeup:** Alarm INT can wake ESP32-C6

## Audio (ES8311 via I2C 0x18 + I2S)

- **ADC (mic):** 24-bit, 97dB SNR, 8-96kHz sample rate
- **DAC (speaker):** 24-bit, 99dB SNR
- **I2S pins:** MCK=19, BCK=20, WS=22, DI=21 (mic→ESP), DO=23 (ESP→speaker)
- **Dual microphone array** on board (noise cancellation capable)
- **XiaoZhi AI compatible** — offline speech recognition + online LLM integration
- **ESP-IDF component:** `espressif/es8311` or via ESP-ADF

## IO Expander (TCA9554 via I2C 0x20)

- **8 I/O pins:** P0-P7
- **P4:** AMOLED display power enable (HIGH = ON)
- **P5:** Touch controller power enable (HIGH = ON)
- **P0-P3, P6-P7:** Unknown/available — check schematic
- **Must init before display/touch** — power-on default is all inputs (HIGH-Z)
- **ESP-IDF component:** `espressif/esp_io_expander_tca9554`

## Wi-Fi

- **Standard:** Wi-Fi 6 (802.11ax) at 2.4GHz
- **Modem sleep default ON** — adds 100-300ms latency. For HTTP servers: `esp_wifi_set_ps(WIFI_PS_NONE)`
- **BLE 5:** Available. Disable with `CONFIG_BT_ENABLED=n` to save ~50KB flash + ~30KB RAM.
- **Thread/Zigbee (802.15.4):** Available — unique advantage of C6 for Matter/smart home

## GPIO Availability

**Nearly all GPIOs are used by onboard peripherals.** No dedicated user GPIO pads.

| If You Sacrifice | GPIOs Freed | Trade-off |
|------------------|-------------|-----------|
| SD Card | 6, 10, 11, 18 | No expandable storage |
| Audio (I2S) | 19, 20, 21, 22, 23 | No mic/speaker |
| UART debug | 16, 17 | No serial monitor (use USB CDC) |

## Typical RAM Budget

```
LVGL draw buffers (×2):   164,864 B  (1/4 screen double-buffer)
LVGL heap:                 65,536 B  (CONFIG_LV_MEM_SIZE_KILOBYTES=64)
Wi-Fi STA:                ~55,000 B
FreeRTOS tasks:           ~40,000 B
HTTP server:               ~8,000 B
Misc (cJSON, drivers):    ~15,000 B
---
Total:                    ~348 KB of 512 KB
Headroom:                 ~164 KB
```

## Gotchas Confirmed / Expected

- **TCA9554 must init before display** — IO expander controls power rails
- **SH8601 even coordinate alignment** — LVGL rounder callback required
- **No PSRAM** — partial rendering only, never full framebuffer
- **No FPU** — avoid float in hot paths
- **No RGB LED** — unlike LCD-1.47, this board has no WS2812
- **I2S pin naming confusion** — DI=21 is mic→ESP (from ES8311 perspective: SDOUT→ESP DIN)
- **6 devices on one I2C bus** — address conflicts with external devices possible
- **`xTaskCreate` not pinned** — single-core chip
- **esp_task_wdt** — not standalone in IDF 5.5, use `esp_system` in REQUIRES
- **LVGL mutations** — only inside `lv_timer` callbacks, never from FreeRTOS tasks
