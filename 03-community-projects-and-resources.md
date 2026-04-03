# Community Projects & Third-Party Resources

## Waveshare ESP32-C6-Touch-AMOLED-1.8

Research compiled April 2026. This board was announced in December 2025 and is relatively new, so the community ecosystem is still growing.

---

## Table of Contents

1. [Board Overview & Key Specs](#board-overview--key-specs)
2. [Official Resources](#official-resources)
3. [GitHub Repositories](#github-repositories)
4. [Display & Touch Driver Libraries](#display--touch-driver-libraries)
5. [LVGL Projects & GUI Development](#lvgl-projects--gui-development)
6. [SquareLine Studio Support](#squareline-studio-support)
7. [ESPHome & Home Assistant](#esphome--home-assistant)
8. [AI Voice Assistant Projects](#ai-voice-assistant-projects)
9. [Zigbee, Thread & Matter Projects](#zigbee-thread--matter-projects)
10. [Blog Posts & Tech Articles](#blog-posts--tech-articles)
11. [YouTube Videos](#youtube-videos)
12. [Forum Discussions](#forum-discussions)
13. [Amazon & Customer Reviews](#amazon--customer-reviews)
14. [Known Issues & Troubleshooting](#known-issues--troubleshooting)
15. [Comparable Boards](#comparable-boards)
16. [MicroPython & CircuitPython](#micropython--circuitpython)
17. [Power Consumption & Battery](#power-consumption--battery)
18. [Summary & Ecosystem Maturity](#summary--ecosystem-maturity)

---

## Board Overview & Key Specs

| Feature | Detail |
|---|---|
| **MCU** | ESP32-C6, single-core 32-bit RISC-V, up to 160 MHz + 20 MHz LP core |
| **RAM** | 512 KB HP SRAM, 16 KB LP SRAM, 320 KB ROM |
| **Flash** | 16 MB external |
| **Display** | 1.8" AMOLED, 368x448 px, 16.7M colors, QSPI |
| **Display Driver** | **SH8601** (QSPI interface) |
| **Touch Controller** | **FT3168 / FT6146** (I2C) |
| **IMU** | QMI8658 6-axis (accelerometer + gyroscope) |
| **RTC** | PCF85063 (with optional backup battery pads) |
| **Audio Codec** | ES8311 (low-power, I2S/I2C) + onboard mic & speaker |
| **PMU** | AXP2101 (battery charging, voltage regulation, power optimization) |
| **GPIO Expander** | TCA9554 (I2C) |
| **Wireless** | Wi-Fi 6 (2.4 GHz), Bluetooth 5.3, Zigbee 3.0, Thread 1.3 |
| **Storage** | TF (microSD) card slot |
| **Battery** | 3.7V MX1.25 lithium battery header, optional 250 mAh Li-ion |
| **USB** | USB-C (power + programming) |
| **Expansion** | 11 solderable pads: USB, UART, BAT, I2C, 5V, 3.3V, GND (1.5 mm pitch) |
| **Price** | ~$28.99 (Waveshare store), ~$30.59 (AliExpress), ~$43.99 (Amazon) |

---

## Official Resources

### Product Pages
- **Waveshare Store**: https://www.waveshare.com/esp32-c6-touch-amoled-1.8.htm
- **Waveshare Documentation Platform**: https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8
- **Arduino Setup Guide**: https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Development-Environment-Setup-Arduino

### Official GitHub Repository
- **waveshareteam/ESP32-C6-Touch-AMOLED-1.8**: https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8
  - 5 stars, 1 fork (as of April 2026)
  - 2 open issues
  - Apache 2.0 license
  - Contains: Firmware, Schematic, Examples (Arduino v3.3.5 + ESP-IDF v5.5.1)

### Schematic
- Available in the GitHub repository under the `Schematic` directory
- Also available as a PDF download from the Waveshare wiki

---

## GitHub Repositories

### Direct Board Support

| Repository | Description | URL |
|---|---|---|
| **waveshareteam/ESP32-C6-Touch-AMOLED-1.8** | Official engineering sample programs with 14 Arduino demos and ESP-IDF examples | https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8 |
| **waveshareteam/Waveshare-ESP32-components** | ESP Component Registry BSPs, display drivers, sensor drivers for Waveshare boards | https://github.com/waveshareteam/Waveshare-ESP32-components |
| **waveshareteam/ESP32-display-support** | Product code summary and support for all Waveshare ESP32 display boards | https://github.com/waveshareteam/ESP32-display-support |

### Related / Sibling Board Repos (Same Hardware Family)

| Repository | Description | URL |
|---|---|---|
| **waveshareteam/ESP32-C6-Touch-AMOLED-2.06** | Sister board (2.06" watch form factor, same chip family) | https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.06 |
| **strnad/ESP32-C6-Touch-AMOLED-2.06-Development-Template** | Community template for the 2.06" board with all hardware initialized, LVGL v9, extensive inline docs. Easily adaptable to the 1.8" board. | https://github.com/strnad/ESP32-C6-Touch-AMOLED-2.06-Development-Template |
| **waveshareteam/ESP32-AIChats** | Waveshare library for ESP32 voice dialogue with cloud AI platforms (DeepSeek, ChatGPT, Doubao). Supports ESP32-S3 AMOLED boards with SH8601. | https://github.com/waveshareteam/ESP32-AIChats |

### Comparable Board Repos (Same Display/Touch Chips)

| Repository | Description | URL |
|---|---|---|
| **Makerfabs/MaTouch-ESP32-S3-AMOLED-with-Touch-1.8-FT3168** | MaTouch 1.8" AMOLED with same FT3168 touch and SH8601 display (ESP32-S3) | https://github.com/Makerfabs/MaTouch-ESP32-S3-AMOLED-with-Touch-1.8-FT3168 |
| **Makerfabs/MaTouch_ESP32-S3-AMOLED-with-Touch-1.8-CHSC6417** | MaTouch 1.8" AMOLED with CO5300 driver (ESP32-S3) | https://github.com/Makerfabs/MaTouch_ESP32-S3-AMOLED-with-Touch-1.8-CHSC6417 |
| **Xinyuan-LilyGO/T-Display-S3-AMOLED-1.43-1.75** | LilyGo AMOLED boards with SH8601 driver (ESP32-S3) | https://github.com/Xinyuan-LilyGO/T-Display-S3-AMOLED-1.43-1.75 |
| **Xinyuan-LilyGO/LilyGo-AMOLED-Series** | LilyGo unified AMOLED series support library | https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series |

---

## Display & Touch Driver Libraries

### SH8601 Display Driver

The SH8601 is a QSPI AMOLED display controller used on this board. Key library options:

| Library | SH8601 Support | Notes | URL |
|---|---|---|---|
| **Arduino_GFX (moononournation)** | Yes | Primary library used by Waveshare examples. SH8601 class with QSPI bus support. Well-established, actively maintained. | https://github.com/moononournation/Arduino_GFX |
| **ESP32_Display_Panel (Espressif)** | Yes | Official Espressif library supporting SH8601. Multi-framework (ESP-IDF, Arduino, MicroPython). | https://github.com/esp-arduino-libs/ESP32_Display_Panel |
| **Waveshare Arduino_DriveBus** | Yes | Bundled with the official demo package. Custom I2C and touch driver library. | Included in demo package |

**Arduino_GFX Discussion on SH8601 Porting**: https://github.com/moononournation/Arduino_GFX/discussions/344
- Key takeaway: Memory allocation issues can prevent hardware-accelerated QSPI mode from working. Software mode works as fallback.
- Init command format: Manufacturer register commands (e.g., "R2A 00 00 01 6F") translate to Arduino_GFX batch operations.

**SH8601 Initialization Sequence** (from ESP32-AIChats project):
1. Sleep out (0x11)
2. Set tear scanline (0x44)
3. Set tear on (0x35)
4. Write control display (0x53)
5. Set column/page address (0x2A, 0x2B)
6. Set brightness (0x51)
7. Display on (0x29)

### CO5300 Display Driver (Alternative/Related)

The CO5300 is a related QSPI AMOLED driver found on some comparable boards (Waveshare 1.75" round, Makerfabs 1.8"). The SH8601 and CO5300 share similar QSPI command structures but are not identical.

| Library | Notes | URL |
|---|---|---|
| **Infineon display-amoled-co5300** | Official CO5300 driver by Infineon; targets PSoC, not ESP32 directly | https://github.com/Infineon/display-amoled-co5300 |

### FT3168 Touch Controller

The FT3168 is a FocalTech self-capacitance I2C touch controller (10 kHz - 400 kHz). Compatible libraries:

| Library | FT3168 Support | Notes | URL |
|---|---|---|---|
| **Waveshare Arduino_DriveBus** | Direct | Bundled with official demos | Included in demo package |
| **SensorLib (v0.3.3)** | Yes | Used by Waveshare for FT3168 + QMI8658 + PCF85063 | Included in demo package |
| **bb_captouch (bitbank2)** | FT6x36 family | Auto-detects FocalTech, GT911, CST820 sensors | https://github.com/bitbank2/bb_captouch |
| **CST816_TouchLib** | CST816 only | Not directly compatible but useful reference for similar touch chips | https://github.com/mjdonders/CST816_TouchLib |

### Other Onboard IC Libraries

| IC | Library | Version | URL |
|---|---|---|---|
| **AXP2101 PMU** | XPowersLib | v0.2.6 | Included in demo package |
| **QMI8658 IMU** | SensorLib | v0.3.3 | Included in demo package |
| **PCF85063 RTC** | SensorLib | v0.3.3 | Included in demo package |
| **ES8311 Audio Codec** | ESP-IDF component | - | ESP-ADF or custom I2S/I2C driver |
| **TCA9554 GPIO Expander** | Adafruit_XCA9554 | v1.0.1 | Included in demo package |

---

## LVGL Projects & GUI Development

### Official LVGL Examples (Bundled with Board)

The Waveshare demo package includes several LVGL-based examples (using LVGL v8.4.0):

| Demo # | Name | Description |
|---|---|---|
| 08 | LVGL_Animation | Slider control for backlight brightness |
| 09 | LVGL_change_background | Button UI to change background colors |
| 10 | LVGL_PCF85063_simpleTime | RTC time display using LVGL |
| 11 | LVGL_QMI8658_ui | Accelerometer data charting |
| 12 | LVGL_Widgets | UI components demo (50-60 FPS achieved) |
| 14 | LVGL_Sqprj | SquareLine Studio UI + auto-rotation + WiFi time |

**Important version note**: The official demos use **LVGL v8.4.0**. LVGL v9 is NOT directly compatible; driver APIs changed significantly between v8 and v9. If migrating to LVGL v9, buffer initialization changed from `lv_disp_draw_buf_init()` to `lv_display_set_buffers()`.

### Community LVGL Work

- **LVGL Forum: SH8601 with LVGL 9 (Waveshare ESP32-S3 Touch AMOLED 1.8")**: https://forum.lvgl.io/t/sh8601-with-lvgl-9-waveshare-esp32-s3-touch-amoled-1-8/21818
  - Documented migration from LVGL 8 to LVGL 9 with the SH8601 display
  - Key issues: pointer dereferencing error in `esp_lcd_panel_io_spi_config_t->user_ctx`, missing display assignment for `lv_display_t` instance
  - Resolution: correct pointer passing through `user_ctx` for interrupt-based flushing

- **LVGL Forum: CO5300/SH8601 driver + LVGL 9 setup**: https://forum.lvgl.io/t/buggy-homebrew-display-firmware-help-me-fix-the-co5300-sh8601-driver-lvgl9-setup/21242
  - Visual corruption and wrong colors encountered
  - Watchdog timer triggers every 1-2 minutes
  - Root cause: LVGL buffer initialization API changes between v8.3 and v9.x

- **strnad/ESP32-C6-Touch-AMOLED-2.06-Development-Template**: https://github.com/strnad/ESP32-C6-Touch-AMOLED-2.06-Development-Template
  - Complete reference implementation with LVGL v9 + ESP-IDF v5.5.1
  - Initializes all hardware: SH8601 display, FT3168 touch, QMI8658 IMU, PCF85063 RTC, ES8311 audio, AXP2101 PMU
  - Modular design, extensive inline documentation
  - Directly adaptable to the 1.8" C6 board (same chip set)

---

## SquareLine Studio Support

Waveshare provides SquareLine Studio integration for their AMOLED boards:

- **Waveshare SquareLine Studio Wiki**: https://www.waveshare.com/wiki/Waveshare_SquareLine_Studio
- **Demo 14 (LVGL_Sqprj)** in the official examples demonstrates:
  - SquareLine UI combined with LVGL
  - QMI8658 sensor for adaptive screen orientation
  - Real-time WiFi time display
  - Touch-based backlight brightness control
- **Espressif BSP + SquareLine**: The Espressif esp-bsp repository includes SquareLine board packages for some Waveshare 7" boards. The AMOLED boards are not yet in the upstream esp-bsp but may be added in the future.
  - https://github.com/espressif/esp-bsp

**Note**: SquareLine Studio uses LVGL, so version alignment is critical. The bundled demos target LVGL v8.4.0.

---

## ESPHome & Home Assistant

### Current Status: No Direct Support for the C6 AMOLED 1.8"

As of April 2026, there is **no official ESPHome configuration** for the ESP32-C6-Touch-AMOLED-1.8 specifically. The SH8601 AMOLED driver is not natively supported in ESPHome's display component. However, there is active community work on similar boards:

### Related ESPHome Discussions

| Topic | Board | Key Findings | URL |
|---|---|---|---|
| Waveshare ESP32-S3-Touch-AMOLED-2.06 Watch | ESP32-S3 AMOLED 2.06" | `qspi_dbi` driver works but slow; `mipi_spi` with AXS15231 model is faster. Touch described as difficult. Battery monitoring unsupported. | https://community.home-assistant.io/t/waveshare-esp32-s3-touch-amoled-2-06-watch/914798 |
| ESP32 C6 LCD+Touch screen | ESP32-C6 LCD boards | General discussion about C6 display support in ESPHome | https://community.home-assistant.io/t/esp32-c6-lcd-touch-screen/955654 |
| ESP32-C6-Touch-LCD-1.47 (JD9853) | ESP32-C6 LCD 1.47" | Some C6 LCD models don't support ESPHome due to display driver limitations | https://community.home-assistant.io/t/esp32-c6-touch-lcd-1-47-jd9853-and-axs5106l-chip/936585 |
| Waveshare ESP32-C6-GEEK | ESP32-C6 (no display) | Working ESPHome config for basic C6 functionality | https://community.home-assistant.io/t/waveshare-esp32-c6-geek/988039 |

### Working ESPHome Example for Related C6 Board

- **hugoferreira/waveshare-esp32-c6-esphome**: ESPHome config for ESP32-C6-LCD-1.47 (ST7789V display, not AMOLED)
  - https://github.com/hugoferreira/waveshare-esp32-c6-esphome
  - Tested with ESPHome v2025.10.5

### Key Challenges for ESPHome + AMOLED

1. **SH8601 QSPI driver not supported** in ESPHome's display platform
2. **FT3168 touch** not in ESPHome's touchscreen component
3. **AXP2101 PMU** has no ESPHome component for battery/power monitoring
4. **Battery life is poor** without sleep mode optimization (reported ~1 hour at full brightness, ~6 hours with aggressive power management on similar boards)

### Potential Workaround

The `mipi_spi` driver in ESPHome may work with custom init sequences, as demonstrated with the S3 AMOLED 2.06" board. Community members found that this approach, while not officially supported, can drive AMOLED displays at 80 MHz data rate.

---

## AI Voice Assistant Projects

### Waveshare ESP32-AIChats

Waveshare has released an AI voice dialogue library that supports their AMOLED boards:

- **Repository**: https://github.com/waveshareteam/ESP32-AIChats
- **Supported platforms**: DeepSeek, ChatGPT, Doubao, Wenxin Yiyan, Tuya Smart
- **Architecture**: ESP32 wake-word detection -> cloud AI model -> text-to-speech
- **Board implementation for 1.8" AMOLED** (documented for ESP32-S3 variant): https://deepwiki.com/waveshareteam/ESP32-AIChats/3-board-implementation:-esp32-s3-touch-amoled-1.8
  - Uses SH8601 via `SH8601Display` class extending `LcdDisplay`
  - ES8311 audio codec for recording + playback
  - LVGL-based UI with user messages, AI responses, and emotion indicators

**Note**: The AIChats library currently supports ESP32-S3 and ESP32-P4. ESP32-C6 support is not confirmed but the hardware (ES8311 codec, dual mics, speaker) is present on the C6 board.

### Related AI Projects

- **xiaozhi-esp32**: MCP-based chatbot supporting ESP32 boards with voice recognition, TTS, multi-language
  - https://github.com/78/xiaozhi-esp32

---

## Zigbee, Thread & Matter Projects

The ESP32-C6 is one of the most versatile chips for smart home protocols. While no specific Zigbee/Thread/Matter projects target the AMOLED 1.8" board directly, the C6 chip's native protocol support makes it ideal:

### General ESP32-C6 Smart Home Resources

| Resource | Description | URL |
|---|---|---|
| Espressif Matter support | ESP32-C6 supports Matter natively over Thread and Wi-Fi | https://www.espressif.com/en/products/socs/esp32-c6 |
| DIY Matter Wi-Fi LED Strip | ESP ZeroCode-based Matter device with ESP32-C6 | https://smarthomescene.com/guides/diy-matter-over-wi-fi-device-with-esp32-c6-and-esp-zerocode/ |
| DFRobot Matter Guide | Connect ESP32-C6 to Matter Network for Smart Lighting | https://community.dfrobot.com/makelog-313735.html |
| Sprig-C6 | ESP32-C6 battery-powered IoT board with Zigbee/Matter/Thread, designed for Home Assistant | https://github.com/Frapais/Sprig-C6 |
| $5 Zigbee Motion Sensor | Building a Zigbee sensor with ESP32-C6 | https://www.xda-developers.com/built-zigbee-motion-sensor-esp32-c6/ |

### Potential Project Ideas with the 1.8" AMOLED Board

- **Zigbee coordinator with display**: Use the AMOLED screen to show connected Zigbee device status
- **Thread border router with touchscreen UI**: Visual interface for Thread network management
- **Matter-enabled smart thermostat**: Leverage the IMU, RTC, and touch display for a wall-mounted controller
- **Smart home dashboard**: Display Home Assistant entities via Wi-Fi 6 with Zigbee sensor data

---

## Blog Posts & Tech Articles

| Article | Source | Date | URL |
|---|---|---|---|
| "ESP32-C6 development board features 1.8-inch AMOLED touch display, built-in mic and speaker, IMU, RTC, and more" | CNX Software | Dec 2025 | https://www.cnx-software.com/2025/12/27/esp32-c6-amoled-development-board-touch-display-built-in-mic-and-speaker-imu-rtc/ |
| "The $30 Swiss Army Knife for Makers" | Hackster.io | ~2026 | https://www.hackster.io/news/the-30-swiss-army-knife-for-makers-71a8ff7d89cc |
| "ESP32-C6 Versions Compared: Best Dev Boards (2026)" | Esp32.co.uk | 2026 | https://esp32.co.uk/full-comparison-of-all-esp32-c6-versions-and-development-boards-2026-guide/ |
| "ESP32 Selection Guide - 2026" | DroneBot Workshop | 2026 | https://dronebotworkshop.com/esp32-2026/ |
| "ESP32-S3 AMOLED Touch Display Unboxing & Setup with LVGL + SquareLine Studio" | Electronic Clinic | ~2025 | https://www.electroniclinic.com/esp32-s3-amoled-touch-display-unboxing-setup-with-lvgl-squareline-studio/ |
| "Getting Started with an ESP32-C6 Waveshare LCD device" (1.47" variant) | Medium (AndroidCrypto) | ~2025 | https://medium.com/@androidcrypto/getting-started-with-an-esp32-c6-waveshare-lcd-device-with-1-47-inch-st7789-tft-display-07804fdc589a |

### Key Takeaways from Articles

- **Hackster.io** describes the board as integrating "just about everything but the kitchen sink" for rapid prototyping
- **CNX Software** provides the most comprehensive technical breakdown, confirming chip IDs (SH8601, FT3168/FT6146, QMI8658, PCF85063, ES8311, AXP2101)
- The board is positioned as ideal for **IoT devices, AI-powered voice assistants, and smart home dashboards**

---

## YouTube Videos

As of April 2026, **dedicated YouTube reviews or tutorials for the ESP32-C6-Touch-AMOLED-1.8 specifically are limited**. The board is relatively new (announced December 2025). Search YouTube directly for:

- "Waveshare ESP32-C6 Touch AMOLED 1.8"
- "ESP32-C6-Touch-AMOLED"
- "Waveshare AMOLED ESP32"

### Related Video Content

Videos for the sibling/comparable boards may provide useful reference:
- Search "Waveshare ESP32-S3 Touch AMOLED" for S3-based AMOLED board reviews
- Search "Waveshare ESP32-C6 AMOLED 2.06 watch" for the watch variant
- Search "ESP32 AMOLED LVGL" for general LVGL development on AMOLED displays
- Waveshare's official YouTube channel may have demo videos

---

## Forum Discussions

### LVGL Forum

| Topic | Relevance | URL |
|---|---|---|
| SH8601 with LVGL 9 (Waveshare ESP32-S3 Touch AMOLED 1.8") | Direct - same display driver and resolution | https://forum.lvgl.io/t/sh8601-with-lvgl-9-waveshare-esp32-s3-touch-amoled-1-8/21818 |
| CO5300/SH8601 driver + LVGL 9 setup issues | Display driver debugging | https://forum.lvgl.io/t/buggy-homebrew-display-firmware-help-me-fix-the-co5300-sh8601-driver-lvgl9-setup/21242 |
| Waveshare 7" ESP32 and SquareLine | SquareLine integration methodology | https://forum.lvgl.io/t/waveshare-7-esp32-and-squareline/22985 |

### Home Assistant Community

| Topic | Relevance | URL |
|---|---|---|
| Waveshare ESP32-S3-Touch-AMOLED-2.06 Watch | ESPHome with AMOLED display (closest reference) | https://community.home-assistant.io/t/waveshare-esp32-s3-touch-amoled-2-06-watch/914798 |
| ESP32 C6 LCD+Touch screen | General C6 display ESPHome support | https://community.home-assistant.io/t/esp32-c6-lcd-touch-screen/955654 |
| ESP32-C6-Touch-LCD-1.47 (JD9853) | C6 display driver challenges | https://community.home-assistant.io/t/esp32-c6-touch-lcd-1-47-jd9853-and-axs5106l-chip/936585 |

### ESP32 Forum / Arduino Forum

| Topic | Relevance | URL |
|---|---|---|
| ESP32-C6 Zero Deep Sleep Current | Power consumption discussion for C6 boards | https://github.com/espressif/arduino-esp32/discussions/12076 |
| ESP32 C6 deep sleep drains battery | Battery issues with C6 | https://forum.arduino.cc/t/esp32-c6-deep-sleep-drains-battery/1327986 |
| Hackaday.io ESP32-C6 Exploration | General C6 exploration project | https://hackaday.io/project/202547-esp32-c6-exploration |

### Reddit

No specific Reddit threads for the ESP32-C6-Touch-AMOLED-1.8 were found in searches. The board is new enough that Reddit discussions may be sparse. Try searching in:
- r/esp32
- r/homeassistant
- r/embedded
- r/arduino

---

## Amazon & Customer Reviews

### Product Listings

| Variant | Price | URL |
|---|---|---|
| With battery | $43.99 | https://www.amazon.com/Waveshare-ESP32-C6-Development-Resolution-Supports/dp/B0GCLSRKMZ |
| Without battery | $43.99 | https://www.amazon.com/Waveshare-ESP32-C6-Development-Resolution-Supports/dp/B0GCMKW68L |

### Customer Feedback (from related AMOLED models)

A review on the C6 AMOLED 2.06" watch variant noted:
> "The AMOLED screen has great contrast, and very deep blacks, the touch layer is responsive, and the ESP32-C6 is powerful enough for all of the projects I could think up for it. It's not as smooth as the S3 for graphics, but it handles simple animations and UI work well enough, and it's definitely the better option for connectivity."

---

## Known Issues & Troubleshooting

### GitHub Issues (as of April 2026)

| # | Title | Status | Date | Description |
|---|---|---|---|---|
| 3 | SD Card support is missing | Open | Mar 2026 | No example code for the onboard TF card slot |
| 2 | Wrong firmware | Open | Jan 2026 | Concerns about incorrect firmware in the repository |

### Common Problems

1. **Flashing/Upload Failures**
   - When serial port is occupied, flashing fails
   - **Fix**: Hold BOOT button, press RESET, release BOOT to enter download mode manually

2. **Blank Flash / USB Instability**
   - Blank flash can cause the USB port to become unstable, leading to constant resets
   - **Fix**: Hold BOOT while connecting power, then release to enter download mode and flash firmware

3. **LVGL Version Incompatibility**
   - Official demos use LVGL v8.4.0
   - LVGL v9 requires significant driver changes (`lv_disp_draw_buf_init()` -> `lv_display_set_buffers()`)
   - Mixing versions causes build failures or runtime crashes

4. **Library Version Dependencies**
   - Strong dependencies between LVGL and driver library versions
   - Use exact versions from the demo package: GFX Library v1.6.4, SensorLib v0.3.3, XPowersLib v0.2.6, lvgl v8.4.0, Arduino_DriveBus v1.0.1

5. **Partition Scheme for Large Demos**
   - Demos 04 (Image) and 14 (SquareLine) require "16M Flash (3MB APP/9.9MB FATFS)" partition scheme
   - Default partition scheme will cause upload failures for these demos

6. **ESP32-C6 Single Core Performance**
   - The C6 has only a single RISC-V core at 160 MHz (vs. ESP32-S3 dual-core at 240 MHz)
   - Graphics-heavy applications may be noticeably slower
   - Customer feedback: "not as smooth as the S3 for graphics, but handles simple animations and UI work well enough"

7. **ESPHome Compatibility**
   - SH8601 QSPI driver not supported in ESPHome
   - FT3168 touch not in ESPHome touchscreen component
   - No native AXP2101 component for power management

---

## Comparable Boards

### Feature Comparison

| Feature | Waveshare ESP32-C6 AMOLED 1.8" | LilyGo T-Display-S3 AMOLED 1.91" | Makerfabs MaTouch ESP32-S3 AMOLED 1.8" | Waveshare ESP32-C6 AMOLED 2.06" |
|---|---|---|---|---|
| **MCU** | ESP32-C6 (RISC-V, 160 MHz, single core) | ESP32-S3 (Xtensa, 240 MHz, dual core) | ESP32-S3 (Xtensa, 240 MHz, dual core) | ESP32-C6 (RISC-V, 160 MHz, single core) |
| **Display** | 1.8" AMOLED 368x448 | 1.91" AMOLED 240x536 | 1.8" AMOLED 368x448 | 2.06" AMOLED 410x502 |
| **Display Driver** | SH8601 | RM67162 | SH8601 / CO5300 | SH8601 |
| **Touch** | FT3168 (I2C) | FT3168 (I2C) | FT3168 / CHSC6417 | FT3168 (I2C) |
| **Wi-Fi** | Wi-Fi 6 | Wi-Fi 4 (b/g/n) | Wi-Fi 4 (b/g/n) | Wi-Fi 6 |
| **Bluetooth** | BLE 5.3 | BLE 5.0 | BLE 5.0 | BLE 5.3 |
| **Zigbee/Thread** | Yes (802.15.4) | No | No | Yes (802.15.4) |
| **IMU** | QMI8658 | No | No | QMI8658 |
| **RTC** | PCF85063 | No | No | PCF85063 |
| **Audio** | ES8311 + mic + speaker | No | No | ES8311 + dual mic + speaker |
| **PMU** | AXP2101 | SY6970 | No | AXP2101 |
| **Battery** | 250 mAh optional | Battery connector | No | 400 mAh recommended |
| **PSRAM** | None | 8 MB | 8 MB | None |
| **Price** | ~$29-44 | ~$20-30 | ~$25-35 | ~$29-40 |
| **Form Factor** | Rectangular dev board | Rectangular dev board | Rectangular dev board | Watch-shaped |

### Key Differentiators

**Waveshare ESP32-C6 AMOLED 1.8" advantages**:
- Wi-Fi 6, Zigbee, Thread support (unique among comparable boards)
- Full sensor suite (IMU, RTC, audio codec, PMU) integrated
- Matter-ready for smart home applications
- Compact all-in-one design

**Waveshare ESP32-C6 AMOLED 1.8" limitations**:
- Single-core 160 MHz CPU (less graphics performance than S3)
- No PSRAM (limits complex GUI and image processing)
- Newer board with smaller community and fewer third-party resources
- ESP32-C6 has less mature tooling compared to ESP32-S3

---

## MicroPython & CircuitPython

### Current Status: No Direct Support

As of April 2026, there is **no CircuitPython board definition or MicroPython firmware** specifically for the ESP32-C6-Touch-AMOLED-1.8.

### Related Resources

| Resource | Description | URL |
|---|---|---|
| CircuitPython ESP32-C6 LCD 1.47 | CircuitPython support for a different Waveshare C6 display board (ST7789) | https://circuitpython.org/board/waveshare_esp32_c6_lcd_1_47/ |
| ptb99/Waveshare-esp32c6 | CircuitPython code for ESP32-C6 + 1.47" Display | https://github.com/ptb99/Waveshare-esp32c6 |
| ESP32 Generic C6 MicroPython | Generic MicroPython firmware for ESP32-C6 (no display support) | https://micropython.org/download/ESP32_GENERIC_C6/ |
| LVGL MicroPython on C6-LCD-1.47 | LVGL + MicroPython on a different Waveshare C6 board | https://gist.github.com/invisiblek/08fc71de9ffc7af3575036102f5b7c76 |

**Challenges**: The SH8601 AMOLED QSPI driver would need to be ported to MicroPython/CircuitPython. The FT3168 touch controller would also need a driver. This is a non-trivial effort given the QSPI display interface.

---

## Power Consumption & Battery

### Measured Data (from similar C6 AMOLED boards)

| Mode | Current | Notes |
|---|---|---|
| Active (full brightness) | ~100-150 mA | Estimated based on similar boards |
| Screen off, active | - | 3-4 hours battery life (400 mAh) |
| Full low-power | - | ~6 hours battery life (400 mAh) |
| Deep sleep (3.7V battery) | **~52 uA** | Optimal; use Li-ion voltage |
| Deep sleep (3.3V supply) | **~392 uA** | AXP2101 not in optimal range at 3.3V |

### Key Findings

- **Always use 3.7V Li-ion battery** for deep sleep; 3.3V supply causes 7.5x higher sleep current due to AXP2101 PMIC behavior
- The 250 mAh optional battery provides roughly 1 hour at full brightness
- Display sleep mode commands are available via SH8601 (0x10 for sleep in, 0x11 for sleep out)
- ESPHome users report difficulty implementing proper display sleep

### References

- ESP32-C6 Deep Sleep with Rust: https://dmelo.eu/blog/esp32c6_deepsleep/
- Lowering Matter device power consumption in ESP32-C6: https://tomasmcguinness.com/2025/01/06/lowering-power-consumption-in-esp32-c6/
- ESP-IDF current consumption measurement guide: https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-guides/current-consumption-measurement-modules.html

---

## ESP Component Registry

Waveshare publishes Board Support Packages (BSPs) to the Espressif ESP Component Registry. While a BSP for the C6 AMOLED 1.8" is not yet published, related BSPs are available:

| Component | Version | URL |
|---|---|---|
| waveshare/esp32_c6_touch_amoled_2_06 | v1.0.0 | https://components.espressif.com/components/waveshare/esp32_c6_touch_amoled_2_06 |
| waveshare/esp32_s3_touch_amoled_1_8 | v1.1.3 | https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_8 |
| waveshare/esp32_s3_touch_amoled_2_06 | v1.0.6 | https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_2_06 |
| waveshare/esp32_s3_touch_amoled_1_75 | v2.0.5 | https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75 |

**Usage** (example for the C6 2.06" BSP, adaptable pattern):
```
idf.py add-dependency "waveshare/esp32_c6_touch_amoled_2_06^1.0.0"
```

A BSP for the C6 AMOLED 1.8" may be published in the future given the pattern of other board support.

---

## Official Arduino Demo Summary

The official demo package provides **14 examples** for Arduino IDE:

| # | Demo | Key Libraries | Features |
|---|---|---|---|
| 01 | HelloWorld | Arduino_GFX | Text display, animation, brightness control |
| 02 | Drawing_board | Arduino_DriveBus, FT3168 | Touch input drawing |
| 03 | GFX_AsciiTable | Arduino_GFX | Character rendering |
| 04 | GFX_FT3168_Image | Arduino_GFX, FT3168 | Touch-triggered image cycling (needs large partition) |
| 05 | GFX_PCF85063_simpleTime | SensorLib | RTC time display |
| 06 | GFX_ESPWiFiAnalyzer | Arduino_GFX, WiFi | WiFi signal strength visualization |
| 07 | GFX_Clock | Arduino_GFX | Analog clock with pointer animation |
| 08 | LVGL_Animation | lvgl v8.4.0 | Slider control for backlight |
| 09 | LVGL_change_background | lvgl v8.4.0 | Button-controlled background |
| 10 | LVGL_PCF85063_simpleTime | lvgl, SensorLib | LVGL + RTC |
| 11 | LVGL_QMI8658_ui | lvgl, SensorLib | IMU data charting |
| 12 | LVGL_Widgets | lvgl v8.4.0 | Widget showcase (50-60 FPS) |
| 13 | ES8311 | ES8311 codec | Audio playback via I2S |
| 14 | LVGL_Sqprj | lvgl, SquareLine | Advanced UI + auto-rotation + WiFi time (needs large partition) |

### Required Library Versions

| Library | Version |
|---|---|
| GFX Library for Arduino | v1.6.4 |
| SensorLib | v0.3.3 |
| XPowersLib | v0.2.6 |
| lvgl | v8.4.0 |
| Arduino_DriveBus | v1.0.1 |
| Adafruit_BusIO | v1.0.1 |
| Adafruit_XCA9554 | v1.0.1 |

---

## Summary & Ecosystem Maturity

### Maturity Assessment (April 2026)

| Area | Status | Notes |
|---|---|---|
| **Official Documentation** | Good | Waveshare docs platform and wiki are comprehensive |
| **Arduino Examples** | Good | 14 demos covering all peripherals |
| **ESP-IDF Support** | Good | Examples for v5.5.1; ESP Component Registry BSP pending |
| **LVGL Integration** | Good (v8) / Developing (v9) | v8.4.0 well-supported; v9 migration requires work |
| **SquareLine Studio** | Available | Demo 14 demonstrates integration |
| **ESPHome** | Not Supported | SH8601 driver missing from ESPHome |
| **MicroPython** | Not Supported | No AMOLED driver for MicroPython |
| **Community Projects** | Early Stage | Few third-party projects; board is <6 months old |
| **GitHub Activity** | Low | 5 stars, 2 issues, no PRs |
| **YouTube Coverage** | Minimal | No dedicated video reviews found |
| **Reddit/Forum Discussion** | Minimal | Very few community discussions |

### Recommendations

1. **For Arduino development**: Use the official demo package as-is. The 14 examples cover all hardware features. Stick to the specified library versions.

2. **For ESP-IDF development**: Use the strnad development template (for the 2.06" board) as a starting point and adapt pin definitions for the 1.8" board. Both share the same chip set.

3. **For LVGL v9**: Reference the LVGL forum threads and the strnad template. Be prepared for API migration work from v8.

4. **For Home Assistant**: Wait for ESPHome SH8601 support, or use the `mipi_spi` workaround documented in the S3 AMOLED 2.06" thread.

5. **For smart home protocols**: This is where the board excels. The ESP32-C6's native Zigbee/Thread/Matter support combined with the AMOLED display makes it uniquely suited for smart home dashboards and controllers.

6. **For AI voice projects**: Monitor the ESP32-AIChats repository for ESP32-C6 support. The hardware (ES8311 + dual mics) is ready.

7. **For maximum community support**: Consider the ESP32-S3 variant (same 1.8" display) which has a larger ecosystem, more PSRAM, and better graphics performance -- but lacks Wi-Fi 6 and Zigbee/Thread.
