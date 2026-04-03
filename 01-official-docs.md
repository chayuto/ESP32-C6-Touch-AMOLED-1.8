# Waveshare ESP32-C6-Touch-AMOLED-1.8 - Official Documentation

> Research compiled from official Waveshare product page, docs portal, wiki, and GitHub repository.
> Date: 2026-04-03

---

## Table of Contents

1. [Product Overview](#product-overview)
2. [Technical Specifications](#technical-specifications)
3. [Onboard Components](#onboard-components)
4. [Display Specifications (AMOLED)](#display-specifications-amoled)
5. [Connectivity](#connectivity)
6. [Power Management](#power-management)
7. [Pin Definition / Pinout](#pin-definition--pinout)
8. [Peripheral Interfaces](#peripheral-interfaces)
9. [Board Versions](#board-versions)
10. [Supported Development Frameworks](#supported-development-frameworks)
11. [Demo Code & Example Projects](#demo-code--example-projects)
12. [Required Libraries (Arduino)](#required-libraries-arduino)
13. [Resources & Downloads](#resources--downloads)
14. [Component Datasheets](#component-datasheets)
15. [FAQ](#faq)
16. [Package Contents](#package-contents)
17. [Pricing & Purchase](#pricing--purchase)
18. [Product Images](#product-images)
19. [Related Products (ESP32 AMOLED Family)](#related-products-esp32-amoled-family)

---

## Product Overview

**Full Name:** ESP32-C6 1.8inch Touch AMOLED Display Development Board, 368 x 448 Resolution, Supports Wi-Fi 6, BLE 5 and Zigbee

**Product ID / SKU:**
- SKU 33305: ESP32-C6-Touch-AMOLED-1.8 (with 3.7V Lithium Battery included)
- SKU 33366: ESP32-C6-Touch-AMOLED-1.8-EN (without Lithium Battery)

**Categories:** WiFi, Bluetooth, ESP32, ESP32-C6, AI Chat, AMOLED

**Description:**
ESP32-C6-Touch-AMOLED-1.8 is a high-performance, highly integrated microcontroller development board designed by Waveshare. It features a tiny form factor (watch-style) with an onboard 1.8-inch capacitive AMOLED display, highly integrated power management IC, 6-axis IMU (3-axis accelerometer and 3-axis gyroscope), RTC, low-power audio codec IC, microphone, speaker, and more. Designed for quick development and integration into products.

Supports offline speech recognition and AI speech interaction, with the ability to connect to online large model platforms such as DeepSeek, Doubao, etc. Compatible with the XiaoZhi AI Application framework (v2.2.4+).

---

## Technical Specifications

### Processor (SoC)

| Parameter | Value |
|-----------|-------|
| SoC | ESP32-C6 |
| Architecture | RISC-V 32-bit |
| Max Operating Frequency | 160 MHz |
| PSRAM | None (not available on ESP32-C6) |

### Memory

| Parameter | Value |
|-----------|-------|
| HP Static RAM (SRAM) | 512 KB |
| LP Static RAM | 16 KB |
| ROM | 320 KB |
| External NOR-Flash | 16 MB |
| Expandable Storage | TF (microSD) card slot |

### Wireless

| Parameter | Value |
|-----------|-------|
| Wi-Fi | Wi-Fi 6 (802.11 b/g/n), 2.4 GHz |
| Bluetooth | Bluetooth 5 (LE), Bluetooth Mesh |
| IEEE 802.15.4 | Zigbee 3.0, Thread |
| Antenna | Onboard PCB antenna (2.4 GHz) |

### USB

| Parameter | Value |
|-----------|-------|
| Connector | USB Type-C |
| Function | Programming, log printing, power supply |
| USB Interface | ESP32-C6 native USB |

---

## Onboard Components

The board contains the following key components (numbered as labeled on the physical board):

| # | Component | Chip/Part | Description |
|---|-----------|-----------|-------------|
| 1 | ESP32-C6 | ESP32-C6 | SoC with WiFi and Bluetooth, up to 160MHz |
| 2 | 6-axis IMU | QMI8658 | 3-axis gyroscope + 3-axis accelerometer |
| 3 | RTC | PCF85063 | Real-Time Clock chip |
| 4 | Power Management | AXP2101 | Highly integrated PMIC |
| 5 | Audio Codec | ES8311 | Low-power audio codec IC |
| 6 | Speaker | - | Onboard speaker for audio output |
| 7 | Backup Battery Pads | - | For connecting backup battery (ensures RTC during main battery replacement) |
| 8 | Battery Header | MX1.25 2PIN | 3.7V Lithium battery connector (supports charge/discharge) |
| 9 | Flash Memory | 16MB NOR-Flash | External storage |
| 10 | Microphone | - | Onboard microphone for audio capture (dual digital microphone array) |
| 11 | Antenna | PCB antenna | Supports 2.4 GHz Wi-Fi and BLE 5 |
| 12 | GPIO Pads | - | Reserved GPIO pads (1.5mm pitch) for expansion |
| 13 | BOOT Button | - | Device startup and function debugging |
| 14 | USB Type-C Port | - | Programming and log printing |
| 15 | PWR Button | - | System power ON/OFF, supports custom functions |

---

## Display Specifications (AMOLED)

| Parameter | Value |
|-----------|-------|
| Display Panel | AMOLED |
| Display Size | 1.8 inch |
| Resolution | 368 x 448 pixels |
| Color Depth | 16.7M colors |
| Brightness | 350 cd/m2 |
| Contrast Ratio | 100,000:1 |
| Viewing Angle | 178 degrees (wide) |
| Communication Interface | QSPI |
| Display Driver IC | **SH8601** |
| Touch Type | Capacitive touch |
| Touch Controller IC | **FT3168 / FT6146** |
| Touch Interface | I2C |

**Note:** The AMOLED screen features high contrast, wide viewing angle, rich colors, fast response, thinner design, and low power consumption compared to traditional LCD displays.

---

## Connectivity

### Wireless Protocols

- **Wi-Fi 6** (802.11 b/g/n) at 2.4 GHz
- **Bluetooth 5 (LE)** with Bluetooth Mesh support
- **IEEE 802.15.4** supporting:
  - Zigbee 3.0
  - Thread

### Wired Interfaces

- **USB Type-C**: Programming, debugging, serial output
- **I2C**: 1 channel (via reserved pads)
- **UART**: 1 channel (via reserved pads)
- **USB**: 1 channel (via reserved pads)
- **TF Card Slot**: For expanded storage, data recording, and media playback

---

## Power Management

| Parameter | Value |
|-----------|-------|
| Power Management IC | **AXP2101** |
| Battery Connector | MX1.25 2PIN header |
| Battery Type | 3.7V Lithium battery |
| Recommended Battery Size | 3.85 x 24 x 28 mm (to fit inside case) |
| Charging | Supported via AXP2101 |
| Discharging | Supported via AXP2101 |
| Backup Battery | Reserved pads for RTC backup battery |
| Power Button | PWR button for system power ON/OFF |

**Features of AXP2101:**
- Multiple voltage outputs
- Battery charging management
- Battery life optimization
- Efficient power management

---

## Pin Definition / Pinout

The board exposes reserved GPIO pads at 1.5mm pitch for external device connection:

- **1x I2C** interface pads
- **1x USB** interface pads
- **1x UART** interface pads

**Note:** A detailed pin definition diagram is available on the product page at:
`/img/devkit/ESP32-C6-Touch-AMOLED-1.8/ESP32-C6-Touch-AMOLED-1.8-details-inter.jpg`

### Internal Pin Assignments (from product description)

| Function | Interface | Details |
|----------|-----------|---------|
| AMOLED Display (SH8601) | QSPI | Display data interface |
| Touch Controller (FT3168/FT6146) | I2C | Touch input |
| IMU (QMI8658) | I2C | Gyroscope + accelerometer data |
| RTC (PCF85063) | I2C | Real-time clock |
| Power Management (AXP2101) | I2C | Power control and monitoring |
| Audio Codec (ES8311) | I2S / I2C | Audio input/output |
| TF Card | SPI | Storage expansion |

---

## Peripheral Interfaces

From the comparison table on the product page, the ESP32-C6-Touch-AMOLED-1.8 has:

| Feature | Available |
|---------|-----------|
| IMU (QMI8658) | Yes |
| RTC (PCF85063) | Yes |
| TF Card Slot | Yes |
| Speaker | Yes |
| Microphone | Yes |
| Battery Header | Yes |
| I2C Pads | Yes |
| UART Pads | Yes |
| USB Pads | Yes |

---

## Board Versions

| Version | SKU | Description |
|---------|-----|-------------|
| ESP32-C6-Touch-AMOLED-1.8 | 33305 | Comes with 3.7V Lithium Battery (can be installed inside the case) |
| ESP32-C6-Touch-AMOLED-1.8-EN | 33366 | Standard version, without Lithium Battery |

Both versions come with a dedicated black case with a removable back cover for easy embedding into projects and DIY designs.

---

## Supported Development Frameworks

### 1. Arduino IDE

- **Board Support:** ESP32 board manager package
- **Arduino Core Version:** v3.3.5 (used in examples)
- **Partition Scheme (for image-heavy demos):** 16M Flash (3MB APP / 9.9MB FATFS)
- **Important:** Disable PSRAM and external Flash features in Arduino IDE settings

### 2. ESP-IDF (Espressif IoT Development Framework)

- **Recommended Version:** ESP-IDF v5.5.0 or higher (examples use v5.5.1)
- **IDE:** Visual Studio Code with ESP-IDF extension (v2.0+ auto-detects environment)

### 3. XiaoZhi AI Framework

- **Firmware Version:** v2.2.4
- **Supports:** Offline speech recognition, AI speech interaction
- **Compatible Platforms:** DeepSeek, Doubao, and other online large model platforms

---

## Demo Code & Example Projects

### GitHub Repository

**URL:** https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8

**Repository Structure:**
```
ESP32-C6-Touch-AMOLED-1.8/
├── Firmware/
├── Schematic/
├── examples/
│   ├── Arduino-v3.3.5/
│   │   ├── examples/
│   │   │   ├── 01_HelloWorld
│   │   │   ├── 02_Drawing_board
│   │   │   ├── 03_GFX_AsciiTable
│   │   │   ├── 04_GFX_FT3168_Image
│   │   │   ├── 05_GFX_PCF85063_simpleTime
│   │   │   ├── 06_GFX_ESPWiFiAnalyzer
│   │   │   ├── 07_GFX_Clock
│   │   │   ├── 08_LVGL_Animation
│   │   │   ├── 09_LVGL_change_background
│   │   │   ├── 10_LVGL_PCF85063_simpleTime
│   │   │   ├── 11_LVGL_QMI8658_ui
│   │   │   ├── 13_LVGL_Widgets
│   │   │   ├── 15_ES8311
│   │   │   └── 16_LVGL_Sqprj
│   │   └── libraries/
│   └── ESP-IDF-v5.5.1/
│       ├── 01_AXP2101          (PMU power data retrieval)
│       ├── 02_PCF85063         (RTC timekeeping functionality)
│       ├── 03_esp-brookesia    (Mobile-style UI system with touch)
│       ├── 04_QMI8658          (Gyroscope and sensor data)
│       └── 05_LVGL_WITH_RAM   (LVGL graphics with DMA, 200-300 FPS)
├── LICENSE (Apache-2.0)
└── README.md
```

### Arduino Examples Summary

| # | Example | Description |
|---|---------|-------------|
| 01 | HelloWorld | Basic introductory example |
| 02 | Drawing_board | Drawing/graphics demo |
| 03 | GFX_AsciiTable | ASCII table graphics display |
| 04 | GFX_FT3168_Image | Touch controller with image display |
| 05 | GFX_PCF85063_simpleTime | RTC time display |
| 06 | GFX_ESPWiFiAnalyzer | WiFi signal strength analyzer |
| 07 | GFX_Clock | Clock display |
| 08 | LVGL_Animation | LVGL animation features |
| 09 | LVGL_change_background | LVGL background customization |
| 10 | LVGL_PCF85063_simpleTime | LVGL-based RTC time display |
| 11 | LVGL_QMI8658_ui | IMU sensor UI integration |
| 13 | LVGL_Widgets | LVGL widget components showcase |
| 15 | ES8311 | Audio codec example |
| 16 | LVGL_Sqprj | SquareLine Studio LVGL project |

### ESP-IDF Examples Summary

| # | Example | Description |
|---|---------|-------------|
| 01 | AXP2101 | Power management unit data retrieval |
| 02 | PCF85063 | RTC timekeeping functionality |
| 03 | esp-brookesia | Mobile-style UI system with touch interactions |
| 04 | QMI8658 | Gyroscope and accelerometer sensor data |
| 05 | LVGL_WITH_RAM | LVGL graphics with DMA acceleration (200-300 FPS) |

---

## Required Libraries (Arduino)

**Critical Note:** There are strong dependencies between versions of LVGL and its driver libraries. A driver written for LVGL v8 may not be compatible with LVGL v9.

| Library | Version | Installation Method |
|---------|---------|-------------------|
| GFX Library for Arduino | v1.6.4 | Library Manager or manual |
| SensorLib | v0.3.3 | Library Manager or manual |
| XPowersLib | v0.2.6 | Library Manager or manual |
| lvgl | v8.4.0 | Library Manager or manual |
| Arduino_DriveBus | v1.0.1 | Manual only |
| Adafruit_BusIO | v1.0.1 | Library Manager or manual |
| Adafruit_XCA9554 | v1.0.1 | Library Manager or manual |
| Mylibrary | -- | Manual (from demo package) |
| ui_a, ui_b, ui_c | -- | Manual (from demo package) |
| lv_conf.h | -- | Manual (from demo package) |

**Installation:** Download the demo package from the GitHub repository and copy all folders from `Arduino/libraries` to your local Arduino libraries folder (typically `C:\Users\<Username>\Documents\Arduino\libraries` on Windows).

---

## Resources & Downloads

### Official Documentation Pages

| Resource | URL |
|----------|-----|
| Docs Portal (main) | https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8 |
| Resources & Documents | https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Resources-And-Documents |
| Arduino Setup Guide | https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Development-Environment-Setup-Arduino |
| ESP-IDF Setup Guide | https://docs.waveshare.com/ESP32-C6-Touch-AMOLCD-1.8/Development-Environment-Setup-ESPIDF |
| AI Tutorials | https://docs.waveshare.com/ESP32-C6-Touch-AMOLCD-1.8/ESP32-AI-Tutorials |
| FAQ | https://docs.waveshare.com/ESP32-C6-Touch-AMOLCD-1.8/FAQ |
| Technical Support | https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Technical-Support |

### Downloadable Files

| Resource | URL |
|----------|-----|
| Schematic (PDF) | https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-1.8/ESP32-C6-Touch-AMOLED-1.8-Schematic.pdf |
| GitHub Demo Code | https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8 |

### Wiki & Product Pages

| Resource | URL |
|----------|-----|
| Wiki Page | https://www.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-1.8 |
| Product Page | https://www.waveshare.com/esp32-c6-touch-amoled-1.8.htm |

**Note:** The wiki page (www.waveshare.com/wiki/...) was still under construction as of research date, with a notice stating "Wiki resources are under urgent production." The new docs portal at docs.waveshare.com contains the active documentation.

---

## Component Datasheets

| Component | Chip | Datasheet URL |
|-----------|------|---------------|
| SoC | ESP32-C6 (EN) | https://documentation.espressif.com/esp32-c6_datasheet_en.pdf |
| SoC | ESP32-C6 (CN) | https://documentation.espressif.com/esp32-c6_datasheet_cn.pdf |
| SoC TRM | ESP32-C6 (EN) | https://documentation.espressif.com/esp32-c6_technical_reference_manual_en.pdf |
| SoC TRM | ESP32-C6 (CN) | https://documentation.espressif.com/esp32-c6_technical_reference_manual_cn.pdf |
| IMU | QMI8658C | https://files.waveshare.com/wiki/common/QMI8658C.pdf |
| RTC | PCF85063A | https://files.waveshare.com/wiki/common/PCF85063A.pdf |
| PMIC | AXP2101 | https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf |
| Audio Codec | ES8311 (Datasheet) | https://files.waveshare.com/wiki/common/ES8311.DS.pdf |
| Audio Codec | ES8311 (User Manual) | https://files.waveshare.com/wiki/common/ES8311.user.Guide.pdf |

**Note:** The display driver (SH8601) and touch controller (FT3168/FT6146) datasheets are not publicly linked by Waveshare.

---

## FAQ

**Q1: How to get more library support for the demo?**
A: Subscribe to the repository and raise an issue at the ESP32-display-support GitHub repo to describe your requirements for engineer assessment.

**Q2: The board is getting too hot. What is the reason?**
A: Three main causes:
1. Ensure the buzzer enable pin is pulled low to prevent excessive power consumption
2. WiFi/Bluetooth usage increases power draw and heat
3. In Arduino IDE, disable PSRAM and external Flash features; consider low-power solutions

**Q3: Why does flashing fail?**
A: Two primary reasons:
1. Serial port is occupied by another application
2. Program crash requires forcing download mode: hold BOOT button during power-on, then restart after flashing

**Q4: How can I check which COM port I am using?**
A: 
- Windows: Device Manager (devmgmt.msc) or Command Prompt
- Linux: `dmesg`, `ls /dev/ttyS*`, `ls /dev/ttyUSB*`, or `setserial`

**Q5: Why is there no output despite successful flashing?**
A: Check USB connection type. Direct USB supports printf/Serial with USB CDC enabled. UART-to-USB supports both without additional configuration.

**Q6: How to use SquareLine Studio to design interfaces?**
A: Refer to the dedicated SquareLine Studio Tutorial documentation on the Waveshare docs portal.

---

## Package Contents

**With Battery (SKU 33305):**
1. ESP32-C6-Touch-AMOLED-1.8 host board x1
2. 3.7V Lithium Battery x1 (350mAh)
3. Screwdriver x1

**Without Battery (SKU 33366):**
1. ESP32-C6-Touch-AMOLED-1.8 host board x1
2. Screwdriver x1

---

## Pricing & Purchase

| Variant | Price |
|---------|-------|
| With 3.7V MX1.25 Lithium Battery (SKU 33305) | $29.99 |
| Without Lithium Battery (SKU 33366) | $28.99 |

**Purchase URL:** https://www.waveshare.com/esp32-c6-touch-amoled-1.8.htm

**Currencies Available:** USD, AUD, GBP, CAD, EUR, JPY

---

## Product Images

All product images are available on the Waveshare product page:

| Image | URL |
|-------|-----|
| Front View | https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/e/s/esp32-c6-touch-amoled-1.8-1_1.jpg |
| View 2 | https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/e/s/esp32-c6-touch-amoled-1.8-2_1.jpg |
| View 3 | https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/e/s/esp32-c6-touch-amoled-1.8-3_1.jpg |
| View 4 | https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/e/s/esp32-c6-touch-amoled-1.8-4_1.jpg |
| View 5 | https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/e/s/esp32-c6-touch-amoled-1.8-5_1.jpg |
| What's On Board | https://www.waveshare.com/img/devkit/ESP32-C6-Touch-AMOLED-1.8/ESP32-C6-Touch-AMOLED-1.8-details-intro.jpg |
| Pin Definition | https://www.waveshare.com/img/devkit/ESP32-C6-Touch-AMOLED-1.8/ESP32-C6-Touch-AMOLED-1.8-details-inter.jpg |
| Outline Dimensions | https://www.waveshare.com/img/devkit/ESP32-C6-Touch-AMOLED-1.8/ESP32-C6-Touch-AMOLED-1.8-details-size.jpg |

---

## Related Products (ESP32 AMOLED Family)

For comparison, here is the full Waveshare ESP32 AMOLED lineup from the product page:

| Model | CPU | PSRAM | Flash | Resolution | Interface | Display Driver | Touch IC | IMU | RTC | TF | Speaker | Mic | Battery |
|-------|-----|-------|-------|------------|-----------|---------------|----------|-----|-----|----|---------| --- |---------|
| **ESP32-C6-Touch-AMOLED-1.8** | **ESP32-C6** | **-** | **16MB** | **368x448** | **QSPI** | **SH8601** | **FT3168/FT6146** | **Y** | **Y** | **Y** | **Y** | **Y** | **Y** |
| ESP32-S3-Touch-AMOLED-1.8 | ESP32-S3R8 | 8MB | 16MB | 368x448 | QSPI | SH8601 | FT3168/FT6146 | Y | Y | Y | Y | Y | Y |
| ESP32-S3-Touch-AMOLED-2.41 | ESP32-S3R8 | 8MB | 16MB | 600x450 | QSPI | RM690B0 | FT6336 | Y | Y | Y | - | - | Y |
| ESP32-C6-Touch-AMOLED-2.16 | ESP32-C6 | - | 16MB | 480x480 | QSPI | CO5300 | CST9220 | Y | Y | Y | Y | Y | Y |
| ESP32-S3-Touch-AMOLED-2.16 | ESP32-S3R8 | 8MB | 16MB | 480x480 | QSPI | CO5300 | CST9220 | Y | Y | Y | Y | Y | Y |
| ESP32-S3-Touch-AMOLED-2.06 | ESP32-S3R8 | 8MB | 32MB | 410x502 | QSPI | CO5300 | FT3168 | Y | Y | Y | Y | Y | Y |
| ESP32-C6-Touch-AMOLED-2.06 | ESP32-C6 | - | 16MB | 410x502 | QSPI | CO5300 | FT3168 | Y | Y | - | Y | Y | Y |
| ESP32-S3-Touch-AMOLED-1.91 | ESP32-S3R8 | 8MB | 16MB | 240x536 | QSPI | RM67162 | FT3168 | Y | - | Y | - | - | Y |
| ESP32-S3-Touch-AMOLED-1.75 | ESP32-S3R8 | 8MB | 16MB | 466x466 | QSPI | CO5300 | CST9217 | Y | Y | Y | Y | Y | Y |
| ESP32-C6-Touch-AMOLED-1.64 | ESP32-C6 | - | 16MB | 280x456 | QSPI | CO5300 | FT6146 | Y | - | Y | - | - | Y |
| ESP32-C6-Touch-AMOLED-1.43 | ESP32-C6 | - | 16MB | 466x466 | QSPI | CO5300 | FT6146 | Y | Y | Y | Y | Y | Y |
| ESP32-C6-Touch-AMOLED-1.32 | ESP32-C6 | - | 16MB | 466x466 | QSPI | CO5300 | CST820 | - | - | - | Y | Y | Y |

---

## Source Information

This document was compiled from the following official Waveshare sources:

1. **Product Page:** https://www.waveshare.com/esp32-c6-touch-amoled-1.8.htm (HTML source successfully fetched)
2. **Docs Portal:** https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8 (content fetched)
3. **Resources Page:** https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Resources-And-Documents (content fetched)
4. **GitHub Repository:** https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8 (content fetched)
5. **Arduino Setup:** https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Development-Environment-Setup-Arduino
6. **ESP-IDF Setup:** https://docs.waveshare.com/ESP32-C6-Touch-AMOLCD-1.8/Development-Environment-Setup-ESPIDF
7. **AI Tutorials:** https://docs.waveshare.com/ESP32-C6-Touch-AMOLCD-1.8/ESP32-AI-Tutorials
8. **FAQ:** https://docs.waveshare.com/ESP32-C6-Touch-AMOLCD-1.8/FAQ
9. **Wiki Page:** https://www.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-1.8 (under construction, redirects to docs portal)

**Note:** The original Waveshare wiki page at www.waveshare.com/wiki/... was still under construction with a message "Wiki resources are under urgent production." Waveshare has migrated documentation to the new docs.waveshare.com portal.
