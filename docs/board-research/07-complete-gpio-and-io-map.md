# Complete GPIO & IO Map - ESP32-C6-Touch-AMOLED-1.8

Source: Official `pin_config.h` from [waveshareteam/ESP32-C6-Touch-AMOLED-1.8](https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8)

> **SD card + display note:** the pin sets are disjoint, but both peripherals
> share `SPI2_HOST` and cannot be used concurrently. See
> [`18-sd-display-spi-sharing.md`](18-sd-display-spi-sharing.md) for the full
> explanation and the Waveshare support exchange.

---

## 1. Official pin_config.h (Verbatim)

```c
#pragma once

#define XPOWERS_CHIP_AXP2101

#define LCD_SDIO0 1
#define LCD_SDIO1 2
#define LCD_SDIO2 3
#define LCD_SDIO3 4
#define LCD_SCLK  0
#define LCD_CS 5
#define LCD_WIDTH 368
#define LCD_HEIGHT 448

// TOUCH
#define IIC_SDA 8
#define IIC_SCL 7
#define TP_INT 15

// ES8311
#define I2S_MCK_IO 19
#define I2S_BCK_IO 20
#define I2S_DI_IO 21
#define I2S_WS_IO 22
#define I2S_DO_IO 23

#define MCLKPIN             19
#define BCLKPIN             20
#define WSPIN               22
#define DOPIN               21
#define DIPIN               23

// SD
const int SDMMC_CLK = 11;
const int SDMMC_CMD = 10;
const int SDMMC_DATA = 18;
const int SDMMC_CS   = 6;
```

---

## 2. Complete GPIO Assignment Table

### ESP32-C6 has GPIOs 0-30 (31 total). Here is every GPIO on this board:

| GPIO | Function | Interface | Peripheral | Notes |
|------|----------|-----------|------------|-------|
| **0** | LCD_SCLK | QSPI Clock | SH8601 AMOLED | Display SPI clock |
| **1** | LCD_SDIO0 | QSPI Data0 | SH8601 AMOLED | Display data line 0 |
| **2** | LCD_SDIO1 | QSPI Data1 | SH8601 AMOLED | Display data line 1 |
| **3** | LCD_SDIO2 | QSPI Data2 | SH8601 AMOLED | Display data line 2 |
| **4** | LCD_SDIO3 | QSPI Data3 | SH8601 AMOLED | Display data line 3 |
| **5** | LCD_CS | QSPI CS | SH8601 AMOLED | Display chip select |
| **6** | SDMMC_CS | SPI CS | SD Card | SD card chip select |
| **7** | IIC_SCL | I2C Clock | Shared I2C bus | All I2C peripherals |
| **8** | IIC_SDA | I2C Data | Shared I2C bus | All I2C peripherals; **strapping pin** |
| **9** | BOOT | Button | Boot button | **Strapping pin** (pull-up, active LOW) |
| **10** | SDMMC_CMD | SDMMC CMD | SD Card | SD card command line |
| **11** | SDMMC_CLK | SDMMC CLK | SD Card | SD card clock |
| **12** | USB D- | USB | Native USB | USB data minus |
| **13** | USB D+ | USB | Native USB | USB data plus |
| **14** | *(unknown)* | | | Not in pin_config.h - likely PWR button or unused |
| **15** | TP_INT | GPIO Input | FT3168 Touch | Touch interrupt (active low); **strapping pin** |
| **16** | UART0 TX | UART | Serial debug | Serial monitor TX |
| **17** | UART0 RX | UART | Serial debug | Serial monitor RX |
| **18** | SDMMC_DATA | SDMMC Data | SD Card | SD card data line |
| **19** | I2S_MCK | I2S MCLK | ES8311 Audio | Master clock |
| **20** | I2S_BCK | I2S BCLK | ES8311 Audio | Bit clock |
| **21** | I2S_DI | I2S DIN | ES8311 Audio | Data in (from mic) |
| **22** | I2S_WS | I2S WS | ES8311 Audio | Word select (L/R) |
| **23** | I2S_DO | I2S DOUT | ES8311 Audio | Data out (to speaker) |
| 24-30 | *(not exposed)* | | | Internal / not connected on this board |

### GPIO Usage Summary

| Category | GPIOs | Count |
|----------|-------|-------|
| QSPI Display | 0, 1, 2, 3, 4, 5 | 6 |
| I2C Bus | 7, 8 | 2 |
| SD Card | 6, 10, 11, 18 | 4 |
| I2S Audio | 19, 20, 21, 22, 23 | 5 |
| Touch INT | 15 | 1 |
| BOOT Button | 9 | 1 |
| USB | 12, 13 | 2 |
| UART Debug | 16, 17 | 2 |
| Unknown/Free | 14, 24-30 | up to 8 |
| **TOTAL ASSIGNED** | | **23** |

---

## 3. User-Accessible Expansion Pads

The board exposes 1.5mm pitch pads for external devices:

| Pad Label | GPIOs | Interface | Notes |
|-----------|-------|-----------|-------|
| I2C | GPIO 7 (SCL), GPIO 8 (SDA) | I2C | Shared with all onboard I2C devices |
| UART | GPIO 16 (TX), GPIO 17 (RX) | UART0 | Shared with serial debug |
| USB | GPIO 12 (D-), GPIO 13 (D+) | USB | Shared with Type-C |

**Critical: No dedicated user GPIO pads.** All exposed pads are shared with onboard functions. To get free GPIOs, you must sacrifice functionality:

| If You Sacrifice | GPIOs Freed | Trade-off |
|------------------|-------------|-----------|
| SD Card | 6, 10, 11, 18 | No expandable storage |
| Audio (I2S) | 19, 20, 21, 22, 23 | No mic/speaker |
| UART debug | 16, 17 | No serial monitor (use USB CDC instead) |

---

## 4. Comparison with LCD-1.47 Board GPIO

| Feature | LCD-1.47 (Old) | AMOLED-1.8 (New) |
|---------|----------------|-------------------|
| Display interface | SPI2 (2 data pins) | QSPI (4 data pins + CLK + CS) |
| Display GPIOs used | 6, 7, 14, 15, 21, 22 | 0, 1, 2, 3, 4, 5 |
| Touch | None | GPIO 15 (INT) + I2C shared |
| I2C bus | Not used by peripherals | Shared: 6 devices on GPIO 7/8 |
| I2S audio | Not present | GPIO 19-23 (5 pins) |
| SD Card | GPIO 4 (CS), 5 (MISO) on SPI2 | GPIO 6, 10, 11, 18 (SDMMC) |
| WS2812 LED | GPIO 8 (RMT) | Not present (no RGB LED) |
| User-safe GPIOs | 0,1,2,3,18,19,20,23 (8 pins) | **Nearly zero free** |
| ADC-capable free | GPIO 0-3 (ADC1_CH0-3) | **None easily available** |
| Backlight PWM | GPIO 22 (LEDC) | Not needed (AMOLED self-emitting) |

**Key difference: The AMOLED-1.8 board uses almost ALL ESP32-C6 GPIOs for onboard peripherals, leaving virtually no free pins for external hardware.** The LCD-1.47 board had 8 safe user GPIOs; this board has close to zero.

---

## 5. Strapping Pins

ESP32-C6 strapping pins that must be at specific levels during boot:

| GPIO | Strapping Function | Boot Level | Board Usage |
|------|-------------------|------------|-------------|
| 8 | Boot mode select | Pull-up | I2C SDA (has pull-up) |
| 9 | Boot mode select | Pull-up | BOOT button (has pull-up) |
| 15 | Boot mode select | Pull-up | Touch INT (external pull-up needed) |

---

## 6. I2S Audio Pin Details

The ES8311 codec uses a standard I2S interface:

```
ESP32-C6 GPIO 19 (MCLK) ───► ES8311 MCLK (Master Clock)
ESP32-C6 GPIO 20 (BCLK) ───► ES8311 SCLK (Serial/Bit Clock)  
ESP32-C6 GPIO 22 (WS)   ───► ES8311 LRCK (Word Select / L-R Clock)
ESP32-C6 GPIO 21 (DIN)  ◄─── ES8311 SDOUT (Mic ADC data → ESP32)
ESP32-C6 GPIO 23 (DOUT) ───► ES8311 SDIN (ESP32 → Speaker DAC data)
```

Note: `DOPIN=21` and `DIPIN=23` in pin_config.h are named from the ES8311's perspective (codec data-out = GPIO 21, codec data-in = GPIO 23), but `I2S_DI_IO=21` and `I2S_DO_IO=23` are from the ESP32's perspective. This is a naming inconsistency in the official code but the wiring is correct:
- GPIO 21 = ESP32 receives audio data (mic input)
- GPIO 23 = ESP32 sends audio data (speaker output)

---

## 7. SD Card Interface

The SD card uses SDMMC (not SPI) protocol, which is faster:

```
ESP32-C6 GPIO 11 (CLK)  ───► SD Card CLK
ESP32-C6 GPIO 10 (CMD)  ◄──► SD Card CMD
ESP32-C6 GPIO 18 (DATA) ◄──► SD Card DAT0
ESP32-C6 GPIO 6  (CS)   ───► SD Card CS (for SPI fallback mode)
```

Note: SDMMC 1-bit mode uses CLK + CMD + DATA. The CS pin (GPIO 6) is provided for SPI fallback mode if SDMMC doesn't work.
