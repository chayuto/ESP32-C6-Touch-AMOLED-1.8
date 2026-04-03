# ESP32-C6 Chip Reference

Comprehensive technical reference for the ESP32-C6 SoC by Espressif Systems, as used in the Waveshare ESP32-C6-Touch-AMOLED-1.8 board.

---

## 1. Overview

The ESP32-C6 is Espressif's first Wi-Fi 6 (802.11ax) SoC, combining 2.4 GHz Wi-Fi 6, Bluetooth 5 (LE), and IEEE 802.15.4 (Thread/Zigbee) radio connectivity in a single chip. Built on 40 nm technology, it targets IoT applications requiring modern wireless protocols, low power consumption, and robust security.

- **Manufacturer:** Espressif Systems
- **Process node:** 40 nm
- **Package options:** QFN40 (5x5 mm, 30 GPIOs) and QFN32 (4x4 mm, 22 GPIOs)
- **Operating voltage:** 3.0 V to 3.6 V
- **Operating temperature:** -40 C to 85 C (standard), -40 C to 105 C (extended, depending on variant)

---

## 2. Processor Cores (Dual RISC-V)

The ESP32-C6 features a **dual-core RISC-V architecture** -- not two identical cores, but a high-performance (HP) core and a low-power (LP) core:

| Core | Architecture | Max Clock | Purpose |
|------|-------------|-----------|---------|
| HP core | 32-bit RISC-V | 160 MHz | Main application processing, WiFi/BLE stack |
| LP core | 32-bit RISC-V | 20 MHz | Ultra-low-power tasks, sensor monitoring during sleep |

### Key CPU characteristics

- Single-issue, in-order pipeline
- Hardware multiplier and divider
- The LP core can run independently while the HP core sleeps, enabling sensor polling and GPIO monitoring at microamp-level current draw
- No floating-point unit (FPU) -- floating-point operations are handled in software
- No SIMD/vector instructions (unlike ESP32-S3's Xtensa with PIE)
- CoreMark score: approximately 497 (HP core at 160 MHz)

---

## 3. Memory Specifications

### On-chip memory

| Memory type | Size | Notes |
|-------------|------|-------|
| ROM | 320 KB | Boot code and core functions |
| HP SRAM | 512 KB | General-purpose, used by application and WiFi/BLE stack |
| LP SRAM | 16 KB | Available to the LP core; retained during deep sleep |

### External flash

- Supports SPI, Dual-SPI, Quad-SPI (QSPI), and QPI flash interfaces
- Common module configurations: 4 MB or 8 MB external flash
- The ESP32-C6-WROOM-1 module variant includes 8 MB SPI flash
- Some SiP (System-in-Package) variants integrate 4 MB flash in the chip package

### No PSRAM support

The ESP32-C6 **does not support external PSRAM**. The C-series chips lack the internal hardware to map PSRAM into their memory space. This is a significant architectural difference from the ESP32-S3, which supports up to 8 MB Octal-SPI PSRAM.

---

## 4. Wireless Connectivity

### 4.1 Wi-Fi 6 (802.11ax)

The ESP32-C6 is Espressif's first chip to support the Wi-Fi 6 standard at 2.4 GHz.

| Feature | Specification |
|---------|--------------|
| Standards | 802.11b/g/n/ax |
| Frequency band | 2.4 GHz |
| Bandwidth | 20 MHz (802.11ax), 20/40 MHz (802.11b/g/n) |
| OFDMA | Supported (uplink and downlink) |
| MU-MIMO | Supported (downlink) |
| Target Wake Time (TWT) | Supported -- enables battery-operated devices to negotiate sleep schedules with the AP |
| Modes | Station (STA), SoftAP, STA+SoftAP, promiscuous |
| Security | WPA, WPA2, WPA3-Personal, WPA3-Enterprise |

**Why Wi-Fi 6 matters for IoT:**
- OFDMA allows the AP to serve multiple IoT devices simultaneously in a single transmission, reducing latency in dense environments
- TWT lets the ESP32-C6 negotiate specific wake-up times with the access point, dramatically improving battery life
- Better performance in congested environments with many SSIDs

### 4.2 Bluetooth 5 (LE)

| Feature | Specification |
|---------|--------------|
| Version | Bluetooth 5 (LE only -- no Classic Bluetooth) |
| Data rates | 1 Mbps, 2 Mbps (high throughput PHY) |
| Coded PHY | Supported (long range -- S=2 and S=8 coding) |
| Advertising extensions | Supported (extended advertising, periodic advertising) |
| LE Power Control | Supported |
| Mesh networking | Bluetooth Mesh supported via ESP-IDF |

**Note:** The ESP32-C6 supports Bluetooth LE only. It does **not** support Bluetooth Classic (BR/EDR), meaning no A2DP audio streaming, no SPP serial profile, etc. For Classic Bluetooth, the original ESP32 or ESP32-S3 (which both support it via their Xtensa cores) are required.

### 4.3 IEEE 802.15.4 (Thread / Zigbee)

| Feature | Specification |
|---------|--------------|
| Standard | IEEE 802.15.4 |
| Thread | Thread 1.3 supported |
| Zigbee | Zigbee 3.0 supported |
| Matter | Full Matter support (over Thread or Wi-Fi) |

The 802.15.4 radio coexists with Wi-Fi and Bluetooth LE. This makes the ESP32-C6 a natural fit for Matter-compliant smart home devices, as it can serve as both a Thread endpoint and a Wi-Fi bridge.

### 4.4 Antenna

- Single antenna shared across Wi-Fi, Bluetooth LE, and 802.15.4 via an integrated coexistence mechanism
- External antenna via RF pin or integrated PCB antenna (depends on module variant)

---

## 5. GPIO Capabilities

### 5.1 Pin count

| Package | Total physical GPIOs | Usable GPIOs |
|---------|---------------------|--------------|
| QFN40 | GPIO0 -- GPIO30 (31 pins) | ~22-24 (some reserved for flash, USB-JTAG) |
| QFN32 | 22 GPIOs | Fewer exposed |

### 5.2 GPIO restrictions

| GPIO pins | Restriction |
|-----------|------------|
| GPIO24-GPIO30 | Reserved for SPI flash on SiP variants; not recommended for other uses |
| GPIO10-GPIO11 | Not exposed on SiP (in-package flash) variants |
| GPIO12-GPIO13 | Default to USB Serial/JTAG; reconfiguring disables USB-JTAG |
| GPIO14 | Not exposed on non-flash variants |
| GPIO4, GPIO5, GPIO8, GPIO9, GPIO15 | **Strapping pins** -- affect boot mode; use with caution |

### 5.3 GPIO features

- All GPIOs support input, output, and interrupt modes
- Internal pull-up and pull-down resistors available
- Configurable drive strength
- GPIO matrix allows flexible peripheral-to-pin mapping (most peripherals can be routed to any GPIO)
- RTC GPIO support on GPIO0-9, GPIO12-13, GPIO15-23 (can operate in deep sleep via LP subsystem)

---

## 6. Peripheral Interfaces

### 6.1 Communication interfaces

| Peripheral | Count | Notes |
|-----------|-------|-------|
| UART | 2 + 1 LP_UART | 2 standard UARTs; 1 low-power UART operable during sleep |
| SPI | 3 (SPI0, SPI1, SPI2) | SPI0/SPI1 reserved for flash; only **SPI2 (GP-SPI)** available for user applications |
| I2C | 1 + 1 LP_I2C | Standard-mode (100 kHz) and Fast-mode (400 kHz); LP_I2C operable during sleep |
| I2S | 1 | Audio interface; supports standard I2S, PCM, TDM modes |
| TWAI (CAN) | 2 | ISO 11898-1 compatible (CAN 2.0); **two** controllers available |
| SDIO | 1 | SDIO 2.0 slave controller |
| USB Serial/JTAG | 1 | Built-in USB-to-serial and JTAG debug; **not USB-OTG** |
| Parallel IO (PARLIO) | 1 | General-purpose parallel interface |

### 6.2 Timers and PWM

| Peripheral | Details |
|-----------|---------|
| System timer | 52-bit |
| General-purpose timers | 2 x 54-bit |
| Watchdog timers | 3 digital + 1 analog |
| LED PWM controller | Up to **6 channels**, 20-bit resolution |
| MCPWM | Motor Control PWM; 1 module |
| RMT | Remote Control Transceiver; TX and RX channels (for IR remote, WS2812 LEDs, etc.) |

### 6.3 Analog peripherals

| Peripheral | Details |
|-----------|---------|
| ADC | 12-bit SAR ADC, up to **7 channels** (ADC1 on GPIO0-GPIO6) |
| DAC | **None** -- the ESP32-C6 has no DAC |
| Temperature sensor | Integrated on-chip |
| Touch sensor | **None** -- the ESP32-C6 has no capacitive touch sensing |

### 6.4 DMA

- General DMA (GDMA) controller with **3 transmit channels** and **3 receive channels**
- Supports memory-to-peripheral and peripheral-to-memory transfers

### 6.5 Event Task Matrix (ETM)

- Hardware event-to-task routing without CPU intervention
- Can link peripheral events to actions (e.g., timer overflow triggers GPIO toggle) at zero CPU cost

---

## 7. Security Features

The ESP32-C6 has comprehensive hardware security:

| Feature | Details |
|---------|---------|
| Secure Boot | RSA-3072 based; ensures only authenticated firmware runs |
| Flash Encryption | AES-128/256-XTS based; protects firmware and data at rest |
| Digital Signature | Hardware-accelerated DS peripheral |
| HMAC | Hardware HMAC peripheral for identity protection |
| Crypto accelerators | AES, SHA, RSA, ECC, RNG hardware acceleration |
| Trusted Execution Environment (TEE) | Privilege separation for secure/non-secure world isolation |
| eFuse | One-time programmable storage for keys and configuration |
| Secure debug | JTAG disable, fault injection protection |
| World Controller | Separates trusted and untrusted code execution |

---

## 8. Low Power Modes and Power Consumption

### 8.1 Power modes

| Mode | Description | Typical current (chip) |
|------|-------------|----------------------|
| **Active** (HP core, no RF) | HP core running at 160 MHz, peripherals enabled, RF off | ~12 mA |
| **Active** (WiFi TX) | HP core + WiFi transmitting | Up to 350 mA peak |
| **Active** (WiFi avg) | HP core + WiFi active, typical | ~60 mA average |
| **Modem Sleep** | HP core active, RF/modem powered off | Up to 38 mA |
| **Light Sleep** | All clocks gated, RAM preserved, state retained | ~180 uA |
| **Deep Sleep** | HP core powered off, HP SRAM not retained; only RTC + LP core + LP SRAM remain | ~7 uA |
| **LP core running** | LP core at 20 MHz during sleep | ~245 uA |

### 8.2 Sleep mode details

**Light Sleep:**
- Digital peripherals, most RAM, and CPUs are clock-gated with reduced supply voltage
- On wake, operation resumes immediately with state preserved
- Wake sources: timer, GPIO, UART, WiFi, Bluetooth

**Deep Sleep:**
- CPUs, most RAM, and all APB_CLK peripherals powered off
- Components that remain powered: RTC controller, LP coprocessor (ULP), RTC FAST memory (LP SRAM)
- Wake sources: RTC timer (microsecond precision), external wake (EXT1 on RTC GPIOs), LP core/ULP coprocessor, GPIO
- LP core can monitor sensors or GPIO pins during deep sleep

### 8.3 Real-world board-level measurements

Actual measurements on development boards (including voltage regulators, pull-ups, etc.) are higher than bare chip specs:

| Mode | Typical board-level current |
|------|---------------------------|
| Active (no WiFi) | ~33 mA |
| Light sleep | ~350 uA |
| Deep sleep | ~72 uA |

**Note:** Board-level deep sleep current is dominated by the voltage regulator quiescent current and other external components, not the ESP32-C6 chip itself.

### 8.4 Wi-Fi power optimization

- **TWT (Target Wake Time):** Negotiate sleep schedules with WiFi 6 AP for maximum battery savings
- **Modem Sleep + Auto Light Sleep:** Maintains WiFi connection while sleeping between beacons
- **DFS (Dynamic Frequency Scaling):** Adjusts CPU/APB frequency to reduce active power

---

## 9. ESP-IDF Support

### 9.1 Version requirements

| ESP-IDF version | ESP32-C6 support |
|----------------|-----------------|
| v5.0 and earlier | **Not supported** |
| **v5.1** | **First version with ESP32-C6 support** (chip rev v0.0 and v0.1) |
| v5.1.5+ | Recommended for chip revision v0.2 |
| v5.2 | Supported; v5.2.4+ recommended for rev v0.2 |
| v5.3 | Supported; v5.3.2+ recommended for rev v0.2 |
| v5.4+ | Full support |
| v5.5.x | Current stable (used by arduino-esp32 v3.3.x) |
| v6.0 | Latest; full support |

**Minimum version: ESP-IDF v5.1**

### 9.2 Chip revisions

| Chip revision | Min ESP-IDF | Recommended |
|--------------|-------------|-------------|
| v0.0, v0.1 | v5.1 | v5.1+ |
| v0.2 | v5.1 | v5.1.5+, v5.2.4+, v5.3.2+, or v5.4+ |

---

## 10. Arduino-ESP32 Core Support

### 10.1 Support status

ESP32-C6 has been supported since **arduino-esp32 v3.0.0** (based on ESP-IDF v5.1).

| Arduino-ESP32 version | Based on ESP-IDF | ESP32-C6 status |
|----------------------|-----------------|----------------|
| v2.x and earlier | v4.x | Not supported |
| **v3.0.0** | v5.1 | **First version with C6 support** |
| v3.3.5 | v5.5.x | Added Waveshare ESP32-C6 Zero board definition |
| **v3.3.7** (latest stable as of early 2026) | v5.5.2 | Fully supported; dedicated `esp32c6-libs` package |

### 10.2 Known issues and considerations

- **PlatformIO:** Some users report "This board doesn't support arduino framework" errors with ESP32-C6 on PlatformIO. This requires using the `espressif32` platform with a version that includes C6 support, and may need manual configuration.
- **Arduino IDE 1.x:** The older Arduino IDE (1.8.x) may not properly show ESP32-C6 boards in the board selector. Arduino IDE 2.x is recommended.
- **Library compatibility:** Not all third-party Arduino libraries are tested with RISC-V ESP32 variants. Libraries using Xtensa-specific assembly or relying on ESP32-S3 features (PSRAM, USB-OTG, touch) will not work on C6.
- **ESPHome:** As of 2025-2026, ESPHome supports ESP32-C6 only via the ESP-IDF framework, not the Arduino framework.

### 10.3 Board manager URL

To use ESP32-C6 in Arduino IDE, add the Espressif board manager URL:
```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

---

## 11. ESP32-C6 vs ESP32-S3: Key Differences

This section highlights what the ESP32-C6 does and does not have compared to the more feature-rich ESP32-S3.

### 11.1 Comparison table

| Feature | ESP32-C6 | ESP32-S3 |
|---------|----------|----------|
| **CPU architecture** | RISC-V (single HP core + LP core) | Xtensa LX7 (dual core) |
| **Max clock speed** | 160 MHz | 240 MHz |
| **CoreMark** | ~497 | ~1182 (dual) / ~614 (single) |
| **SIMD / vector** | No | Yes (128-bit PIE) |
| **SRAM** | 512 KB (HP) + 16 KB (LP) | 512 KB |
| **PSRAM** | **Not supported** | Up to 8 MB Octal-SPI |
| **External flash** | SPI/QSPI up to 4-8 MB | Octal-SPI, higher throughput |
| **GPIOs** | Up to 30 | Up to 45 |
| **ADC channels** | 7 (12-bit) | 20 (two 12-bit SAR ADCs) |
| **DAC** | **None** | **None** (removed in S3; was in original ESP32) |
| **Touch sensors** | **None** | 14 capacitive touch pins |
| **USB** | USB Serial/JTAG only | **USB-OTG** (full-speed) + USB Serial/JTAG |
| **Camera interface** | **None** | DVP camera interface |
| **LCD interface** | **None** | Parallel LCD (RGB/i8080) |
| **SD/MMC** | SDIO 2.0 slave only | SD/MMC host |
| **I2C** | 1 + 1 LP | 2 |
| **SPI (user)** | 1 (SPI2) | 2 (SPI2, SPI3) |
| **UART** | 2 + 1 LP | 3 |
| **TWAI (CAN)** | 2 | 1 |
| **Wi-Fi** | **Wi-Fi 6 (802.11ax)** | Wi-Fi 4 (802.11n) |
| **Bluetooth** | BLE 5 only | BLE 5 + Classic (BR/EDR) |
| **802.15.4 (Thread/Zigbee)** | **Yes** | No |
| **Matter support** | Via Thread and/or WiFi | Via WiFi only |
| **AI/ML acceleration** | None | Vector instructions for NN |
| **Deep sleep current** | ~7 uA (chip) | ~7-8 uA (chip) |

### 11.2 What the ESP32-C6 lacks vs ESP32-S3

1. **No PSRAM** -- Cannot buffer large data sets, frame buffers, or ML models in external RAM
2. **No USB-OTG** -- Only USB-Serial/JTAG for debugging; no USB host/device functionality
3. **No camera/LCD interfaces** -- No native DVP camera or parallel RGB LCD ports
4. **No touch sensing** -- No capacitive touch pins
5. **No Bluetooth Classic** -- BLE only; no A2DP audio, no SPP serial
6. **Lower CPU performance** -- Single 160 MHz RISC-V vs dual 240 MHz Xtensa with SIMD
7. **Fewer GPIOs** -- 30 vs 45
8. **Fewer ADC channels** -- 7 vs 20
9. **Only 1 user SPI bus** -- SPI2 only (SPI0/SPI1 reserved for flash)
10. **No SD/MMC host** -- Only SDIO slave; cannot directly host an SD card

### 11.3 What the ESP32-C6 has that the ESP32-S3 does not

1. **Wi-Fi 6 (802.11ax)** -- OFDMA, TWT, MU-MIMO for better IoT connectivity
2. **IEEE 802.15.4** -- Native Thread and Zigbee support
3. **Dedicated LP core** -- Independent 20 MHz RISC-V for ultra-low-power tasks
4. **LP peripherals** -- LP_UART, LP_I2C, LP_GPIO available during deep sleep
5. **Two TWAI (CAN) controllers** -- vs one on ESP32-S3
6. **Trusted Execution Environment (TEE)** -- Hardware privilege separation
7. **Lower cost** -- Generally less expensive than ESP32-S3

### 11.4 When to choose ESP32-C6

- Smart home / Matter devices (Thread + WiFi bridge)
- Battery-powered IoT sensors (TWT, deep sleep with LP core)
- Dense WiFi environments (WiFi 6 OFDMA)
- Zigbee coordinators/endpoints
- Industrial CAN bus applications (dual TWAI)
- Cost-sensitive, connectivity-focused designs

### 11.5 When to choose ESP32-S3 instead

- Camera / vision applications
- Display-heavy applications (parallel RGB LCD)
- USB host/device requirements
- AI/ML edge inference (SIMD, PSRAM for models)
- Applications needing many GPIO or ADC channels
- Audio applications requiring Bluetooth Classic (A2DP)
- Applications needing large memory buffers (PSRAM)

---

## 12. Known Gotchas and Practical Notes

### 12.1 GPIO strapping pins

GPIO4, GPIO5, GPIO8, GPIO9, and GPIO15 are strapping pins. Their state during boot determines the boot mode. Connecting external devices to these pins can cause boot failures if they pull the pin to an unexpected level. Always check the datasheet for safe default states.

### 12.2 USB-JTAG pins

GPIO12 and GPIO13 default to USB Serial/JTAG mode. If you reconfigure them for other purposes, you lose USB-JTAG debug access. This is important on boards that use USB-JTAG for both programming and debugging.

### 12.3 Limited SPI buses

Only SPI2 is available for user peripherals (SPI0 and SPI1 are reserved for flash). If your application needs to communicate with multiple SPI devices, they must share SPI2 with chip-select multiplexing.

### 12.4 No PSRAM means memory discipline

With only 512 KB SRAM total (shared with the WiFi/BLE stack), memory-intensive applications need careful planning. The WiFi stack alone can consume 50-100 KB. Use `heap_caps_get_free_size()` to monitor available memory.

### 12.5 Flash GPIO conflict on SiP variants

On modules with in-package flash (SiP), GPIO24-GPIO30 are used for the flash interface and are unavailable for user applications. GPIO10-GPIO11 are also not exposed.

### 12.6 WiFi 6 requires a WiFi 6 AP

While the ESP32-C6 is backward compatible with 802.11b/g/n, the WiFi 6-specific benefits (OFDMA, TWT) only work when connected to a WiFi 6 (802.11ax) capable access point.

### 12.7 No analog output

Without a DAC, generating analog signals requires an external DAC chip (e.g., MCP4725 over I2C) or using PWM with an RC low-pass filter for rough analog approximation.

### 12.8 Library compatibility

When using Arduino or other frameworks, verify that libraries are compatible with the RISC-V architecture. Libraries using inline Xtensa assembly, ESP32-S3-specific registers, or features like touch, PSRAM, or USB-OTG will not work.

### 12.9 Deep sleep current on development boards

While the chip itself draws ~7 uA in deep sleep, development boards typically draw 50-100 uA or more due to voltage regulator quiescent current, USB bridge chips, LEDs, and pull-up resistors. For true low-power designs, custom PCBs with efficient regulators are necessary.

### 12.10 Single-core only for application code

Despite having two RISC-V cores, the ESP32-C6 is effectively a **single-core** chip for general application code. The LP core is designed for simple monitoring tasks during sleep, not for running full application logic. This is different from the ESP32-S3's dual symmetric cores, where both can run FreeRTOS tasks.

---

## 13. Official Documentation Links

### Espressif datasheets and manuals

- [ESP32-C6 Product Page](https://www.espressif.com/en/products/socs/esp32-c6)
- [ESP32-C6 Series Datasheet (PDF)](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)
- [ESP32-C6 Technical Reference Manual (PDF)](https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf)
- [ESP32-C6-WROOM-1 Module Datasheet (PDF)](https://www.espressif.com/sites/default/files/documentation/esp32-c6-wroom-1_wroom-1u_datasheet_en.pdf)
- [ESP32-C6-MINI-1 Module Datasheet (PDF)](https://www.espressif.com/sites/default/files/documentation/esp32-c6-mini-1_mini-1u_datasheet_en.pdf)

### ESP-IDF programming guides

- [ESP-IDF Programming Guide for ESP32-C6 (stable)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/index.html)
- [ESP-IDF Get Started - ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/index.html)
- [GPIO & RTC GPIO - ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/gpio.html)
- [Sleep Modes - ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/system/sleep_modes.html)
- [Current Consumption Measurement - ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-guides/current-consumption-measurement-modules.html)
- [Low Power Mode in WiFi Scenarios - ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-guides/low-power-mode/low-power-mode-wifi.html)
- [ESP-IDF Chip Revision Compatibility](https://github.com/espressif/esp-idf/blob/master/COMPATIBILITY.md)

### Arduino-ESP32

- [Arduino-ESP32 GitHub Repository](https://github.com/espressif/arduino-esp32)
- [Arduino-ESP32 Releases](https://github.com/espressif/arduino-esp32/releases)
- [Arduino-ESP32 Documentation](https://espressif.github.io/arduino-esp32/)

### Hardware design

- [ESP32-C6 Hardware Design Guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c6/product-overview.html)
- [ESP32-C6-DevKitC-1 User Guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html)

### Workshop and learning

- [ESP-IDF with ESP32-C6 Workshop (Espressif Developer Portal)](https://developer.espressif.com/workshops/esp-idf-with-esp32-c6/introduction/)

### Announcements

- [Espressif ESP32-C6 Announcement](https://www.espressif.com/en/news/ESP32_C6)

---

## 14. Quick Reference Card

```
ESP32-C6 at a Glance
====================
CPU:            RISC-V 160 MHz (HP) + RISC-V 20 MHz (LP)
SRAM:           512 KB (HP) + 16 KB (LP)
ROM:            320 KB
Flash:          External SPI/QSPI, typically 4-8 MB
PSRAM:          Not supported
WiFi:           802.11b/g/n/ax (WiFi 6), 2.4 GHz, 20 MHz BW
Bluetooth:      BLE 5 (no Classic)
802.15.4:       Thread 1.3 + Zigbee 3.0
GPIOs:          Up to 30 (QFN40) / 22 (QFN32)
ADC:            7-channel 12-bit SAR
DAC:            None
Touch:          None
USB:            Serial/JTAG only (no OTG)
SPI (user):     1 (SPI2)
I2C:            1 + 1 LP
UART:           2 + 1 LP
I2S:            1
TWAI (CAN):     2
LED PWM:        6 channels
MCPWM:          1
Deep sleep:     ~7 uA (chip)
Package:        QFN40 (5x5mm) / QFN32 (4x4mm)
Voltage:        3.0-3.6V
Min ESP-IDF:    v5.1
Min Arduino:    arduino-esp32 v3.0.0
```
