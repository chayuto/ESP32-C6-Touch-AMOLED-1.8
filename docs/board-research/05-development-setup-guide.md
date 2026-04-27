# Development Environment Setup Guide: Waveshare ESP32-C6-Touch-AMOLED-1.8

## Board Overview

| Specification | Details |
|---|---|
| **SoC** | ESP32-C6 (32-bit RISC-V, up to 160 MHz) |
| **Display** | 1.8" AMOLED, 368x448 pixels, 16.7M colors |
| **Display Driver** | SH8601 (QSPI interface) |
| **Touch Controller** | FT3168 / FT6146 (I2C, address 0x38) |
| **Flash** | 16 MB external NOR-Flash |
| **RAM** | 512 KB HP SRAM, 16 KB LP SRAM, 320 KB ROM |
| **PMU** | AXP2101 (I2C) |
| **IMU** | QMI8658 6-axis (I2C, address 0x6B) |
| **RTC** | PCF85063 (I2C, address 0x51) |
| **Audio Codec** | ES8311 (I2S + I2C) |
| **Wireless** | Wi-Fi 6 (802.11ax), Bluetooth 5 (LE), Zigbee 3.0, Thread |
| **Storage** | MicroSD card slot |
| **Battery** | MX1.25 connector for 3.7V lithium |
| **USB** | Type-C (programming and debugging) |

### GPIO Pin Mapping

Extracted from the official Waveshare `pin_config.h`:

| Function | GPIO |
|---|---|
| **LCD_SCLK** (QSPI Clock) | GPIO 0 |
| **LCD_SDIO0** (QSPI Data 0) | GPIO 1 |
| **LCD_SDIO1** (QSPI Data 1) | GPIO 2 |
| **LCD_SDIO2** (QSPI Data 2) | GPIO 3 |
| **LCD_SDIO3** (QSPI Data 3) | GPIO 4 |
| **LCD_CS** (Chip Select) | GPIO 5 |
| **SDMMC_CS** (SD Card CS) | GPIO 6 |
| **IIC_SCL** (I2C Clock) | GPIO 7 |
| **IIC_SDA** (I2C Data) | GPIO 8 |
| **SDMMC_CMD** (SD Card CMD) | GPIO 10 |
| **SDMMC_CLK** (SD Card CLK) | GPIO 11 |
| **TP_INT** (Touch Interrupt) | GPIO 15 |
| **SDMMC_DATA** (SD Card Data) | GPIO 18 |
| **I2S_MCK_IO** (Audio Master Clock) | GPIO 19 |
| **I2S_BCK_IO** (Audio Bit Clock) | GPIO 20 |
| **I2S_DI_IO** (Audio Data In) | GPIO 21 |
| **I2S_WS_IO** (Audio Word Select) | GPIO 22 |
| **I2S_DO_IO** (Audio Data Out) | GPIO 23 |

**Power Management IC:** AXP2101 (`XPOWERS_CHIP_AXP2101`)

> **Source:** https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8/blob/main/examples/Arduino-v3.3.5/libraries/Mylibrary/pin_config.h

---

## 1. Arduino IDE / PlatformIO Arduino

### 1.1 Arduino IDE Setup

#### Board Manager URL

Add the Espressif ESP32 board package URL in **File > Preferences > Additional Boards Manager URLs**:

**Stable release:**
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

**Development release (for latest features):**
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_dev_index.json
```

Then open **Tools > Board > Boards Manager**, search for "esp32" by Espressif Systems, and install.

#### Board Selection

- **Board:** `ESP32C6 Dev Module` (under Tools > Board > esp32)
- **Flash Size:** 16MB (under Tools > Flash Size)
- **Partition Scheme:** `16M Flash (3MB APP/9.9MB FATFS)` -- required for demos with large images (e.g., `04_GFX_FT3168_Image` and `16_LVGL_Sqprj`)
- **Upload Speed:** 921600
- **USB CDC On Boot:** Enabled
- **Port:** Select the USB serial port for the board

#### Required Arduino Libraries (Known Working Versions)

These exact versions are tested and recommended by Waveshare. Mixing versions may cause compilation failures or runtime exceptions due to tight inter-library dependencies.

| Library | Version | Install Method |
|---|---|---|
| **GFX Library for Arduino** | v1.6.4 | Arduino Library Manager |
| **lvgl** | v8.4.0 | Arduino Library Manager |
| **SensorLib** | v0.3.3 | Arduino Library Manager |
| **XPowersLib** | v0.2.6 | Arduino Library Manager |
| **Adafruit_BusIO** | v1.0.1 | Arduino Library Manager |
| **Adafruit_XCA9554** | v1.0.1 | Arduino Library Manager |
| **Arduino_DriveBus** | v1.0.1 | Manual install from demo package |
| **Mylibrary** (pin macros) | -- | Manual install from demo package |
| **lv_conf.h** | -- | Manual install from demo package |

**Manual installation path (Windows):** `C:\Users\<Username>\Documents\Arduino\libraries`

Copy all folders from the official demo package `Arduino/libraries` directory into this path.

> **Important:** LVGL v8 and v9 are not interchangeable. The Waveshare examples use LVGL v8.4.0. Drivers written for LVGL v8 are not compatible with LVGL v9.

#### Alternative: ESP32_Display_Panel Library

Espressif's official `ESP32_Display_Panel` library (available on Arduino Library Manager and ESP Component Registry) supports both the SH8601 display controller and the ESP32-C6 SoC. It can serve as an alternative to the Waveshare-provided Arduino_DriveBus library.

- Repository: https://github.com/esp-arduino-libs/ESP32_Display_Panel
- Configuration files: `esp_panel_board_custom_conf.h`, `esp_panel_drivers_conf.h`

#### Available Arduino Examples

From the official repository (`examples/Arduino-v3.3.5/examples/`):

| # | Example | Description |
|---|---|---|
| 01 | HelloWorld | Basic display test |
| 02 | Drawing_board | Touch-based drawing |
| 03 | GFX_AsciiTable | ASCII character display |
| 04 | GFX_FT3168_Image | Image display with touch (needs large partition) |
| 05 | GFX_PCF85063_simpleTime | RTC time display |
| 06 | GFX_ESPWiFiAnalyzer | WiFi analyzer |
| 07 | GFX_Clock | Analog clock |
| 08 | LVGL_Animation | LVGL animation demo |
| 09 | LVGL_change_background | LVGL background change |
| 10 | LVGL_PCF85063_simpleTime | LVGL + RTC time |
| 11 | LVGL_QMI8658_ui | LVGL + IMU orientation |
| 13 | LVGL_Widgets | LVGL widget showcase |
| 15 | ES8311 | Audio codec demo |
| 16 | LVGL_Sqprj | SquareLine Studio project (needs large partition) |

### 1.2 PlatformIO (Arduino Framework)

The official PlatformIO `espressif32` platform has limited Arduino framework support for ESP32-C6. Use the **pioarduino** community fork for full Arduino framework support.

#### platformio.ini

```ini
[env:esp32-c6-touch-amoled-1.8]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/54.03.20/platform-espressif32.zip
board = esp32-c6-devkitm-1
framework = arduino
board_build.flash_size = 16MB
board_build.partitions = default_16MB.csv
upload_port = /dev/ttyACM0          ; adjust for your OS
monitor_speed = 115200
build_flags =
    -D ARDUINO_USB_MODE=1
    -D ARDUINO_USB_CDC_ON_BOOT=1
    -D BOARD_HAS_PSRAM=0
lib_deps =
    moononournation/GFX Library for Arduino@1.6.4
    lvgl/lvgl@8.4.0
    lewisxhe/SensorLib@0.3.3
    lewisxhe/XPowersLib@0.2.6
```

> **Note on pioarduino:** This fork maintains Arduino ESP32 Core 3.x support for ESP32-C6, ESP32-C5, ESP32-H2, and ESP32-P4. The migration from Arduino 2.x to 3.x may cause issues with certain third-party libraries. Consult the [Arduino ESP32 migration guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/migration_guides/2.x_to_3.0.html).

#### PlatformIO with ESP-IDF Framework (Official Support)

If you prefer ESP-IDF without the Arduino overhead:

```ini
[env:esp32-c6-idf]
platform = espressif32
board = esp32-c6-devkitc-1
framework = espidf
board_build.flash_size = 16MB
monitor_speed = 115200
```

---

## 2. ESP-IDF (Espressif IoT Development Framework)

### 2.1 Minimum Version

- **Minimum:** ESP-IDF v5.1 (first version with ESP32-C6 support)
- **Recommended:** ESP-IDF v5.5.1 (version used by official Waveshare examples)

### 2.2 Installation

Install ESP-IDF following the official guide at https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/index.html

The recommended IDE is **VS Code with the Espressif IDF extension**.

### 2.3 Project Setup

#### Set target

```bash
idf.py set-target esp32c6
```

#### Top-level CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my_project)
```

#### main/CMakeLists.txt

```cmake
idf.py_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
)
```

#### main/idf_component.yml (Component Dependencies)

For using the SH8601 display driver from the ESP Component Registry:

```yaml
dependencies:
  espressif/esp_lcd_sh8601: ">=1.0.0"
  # Optional additional components:
  # espressif/esp32_display_panel: ">=1.0.0"
  idf:
    version: ">=5.1.0"
```

Install via CLI:
```bash
idf.py add-dependency "espressif/esp_lcd_sh8601"
```

### 2.4 menuconfig Settings

Run `idf.py menuconfig` and configure the following:

#### Flash Configuration
```
Serial flasher config -->
    Flash size: 16 MB
```

#### Performance Optimization
```
Compiler options -->
    Optimization level: Performance (-O2)
```

#### SPI/QSPI Display Settings
The SH8601 display uses QSPI (SPI2_HOST). Ensure SPI is enabled:
```
Component config -->
    Driver Configurations -->
        SPI Configuration -->
            [*] Enable SPI Master
```

#### LVGL Settings (if using LVGL)
```
Component config -->
    LVGL configuration -->
        Color settings -->
            Color depth: 16 (RGB565)
        Memory settings -->
            LV_MEM_SIZE: adjust based on available RAM
        Display -->
            Default display refresh period: 15 (for ~60 FPS)
```

### 2.5 sdkconfig.defaults

Based on the related ESP32-C6-Touch-AMOLED-2.06 development template:

```ini
# Performance
CONFIG_COMPILER_OPTIMIZATION_PERF=y

# Flash
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# LVGL display refresh
CONFIG_LV_DEF_REFR_PERIOD=15

# C++ support (needed for XPowersLib and some components)
CONFIG_COMPILER_CXX_EXCEPTIONS=y
CONFIG_COMPILER_CXX_RTTI=y

# Audio (if using ES8311)
CONFIG_BSP_I2S_NUM=1
```

### 2.6 SH8601 Display Initialization (ESP-IDF)

Key points for the SH8601 driver in ESP-IDF:

1. **Coordinate alignment:** x_start, y_start, x_end, y_end must be divisible by 2 (SH8601 hardware requirement). Implement a rounder callback for LVGL.
2. **QSPI bus config:** Use `SH8601_PANEL_BUS_QSPI_CONFIG()` macro.
3. **Panel IO config:** Use `SH8601_PANEL_IO_QSPI_CONFIG()` macro.
4. **Color format:** RGB565 (16-bit), set via LCD command 0x3A.
5. **DMA:** Enable with `SPI_DMA_CH_AUTO`.

```c
// Typical initialization flow:
// 1. Initialize SPI bus with DMA
// 2. Create panel IO: esp_lcd_new_panel_io_spi()
// 3. Install driver: esp_lcd_new_panel_sh8601()
// 4. Reset, init, display on
```

### 2.7 Build and Flash

```bash
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 2.8 Available ESP-IDF Examples

From the official repository (`examples/ESP-IDF-v5.5.1/`):

| # | Example | Description |
|---|---|---|
| 01 | AXP2101 | Power management IC demo |
| 02 | PCF85063 | RTC demo |
| 03 | esp-brookesia | ESP component demo |
| 04 | QMI8658 | IMU sensor demo |
| 05 | LVGL_WITH_RAM | LVGL display with RAM buffer |

---

## 3. MicroPython

### 3.1 Firmware Availability

Official MicroPython firmware for ESP32-C6 is available:

- **Download page:** https://micropython.org/download/ESP32_GENERIC_C6/
- **Latest stable:** v1.27.0 (2025-12-09)
- **Preview:** v1.28.0-preview builds available

### 3.2 Flash Instructions

```bash
# Install esptool
pip install esptool

# Erase flash
esptool.py --port /dev/ttyACM0 erase_flash

# Flash firmware
esptool.py --port /dev/ttyACM0 --baud 460800 write_flash 0 ESP32_GENERIC_C6-20251209-v1.27.0.bin
```

If flashing fails, remove `--baud 460800` to use the slower default speed.

### 3.3 Display Driver for SH8601

There is **no official MicroPython driver** for the SH8601 AMOLED display on ESP32-C6. Options:

#### Option A: Community Driver (dobodu/Lilygo-Waveshare_Amoled-Micropython)

- Repository: https://github.com/dobodu/Lilygo-Waveshare_Amoled-Micropython
- Supports SH8601 QSPI displays
- **Limitation:** Currently targets ESP32-S3 only. The driver is compiled as a C module into the MicroPython firmware using `USER_C_MODULES` with the `MODULE_AMOLED_ENABLED` flag.
- Requires MicroPython 1.26.1+ and ESP-IDF 5.5.2 toolchain
- Build command: `make BOARD=ESP32_GENERIC_S3`
- **For ESP32-C6:** Would require porting the build to target `ESP32_GENERIC_C6` and testing/adapting pin configurations. This is not yet confirmed working.
- Provides drawing functions: fill, fill_rect, rect, circle, pixel, lines, text, bitmap, brightness control

#### Option B: Custom Firmware Build

Build a custom MicroPython firmware with the `esp_lcd_sh8601` component integrated as a C module:

1. Clone MicroPython source
2. Set up ESP-IDF v5.5.x toolchain
3. Add SH8601 driver as a user C module
4. Build with `make BOARD=ESP32_GENERIC_C6 USER_C_MODULES=...`

#### Option C: Generic SPI/Framebuffer Approach

Use MicroPython's built-in `machine.SPI` to bit-bang the SH8601 initialization sequence and frame data over QSPI. This is complex and not performant, but possible for basic display output.

### 3.4 Touch Driver

The FT3168/FT6146 touch controller communicates over I2C (address 0x38). It is compatible with the FocalTech FT5x06/FT6x06 family, for which community MicroPython drivers exist. Use standard `machine.I2C` to communicate.

```python
from machine import I2C, Pin

i2c = I2C(0, scl=Pin(7), sda=Pin(8), freq=100000)
# FT3168 at address 0x38
devices = i2c.scan()
print(devices)  # Should show [56] (0x38)
```

### 3.5 Peripheral Drivers in MicroPython

| Peripheral | MicroPython Support |
|---|---|
| QMI8658 (IMU) | Community drivers available for I2C |
| PCF85063 (RTC) | Community drivers available for I2C |
| AXP2101 (PMU) | Limited; may need custom driver |
| ES8311 (Audio) | I2S supported on ESP32-C6 since MicroPython v1.24+ |
| SD Card | Built-in `machine.SDCard` support |

---

## 4. CircuitPython

### 4.1 ESP32-C6 Support Status

CircuitPython has **official ESP32-C6 support** as of CircuitPython 9.x. The latest release is CircuitPython 10.1.3 (February 2026).

Boards with official CircuitPython ESP32-C6 support include:
- Espressif ESP32-C6-DevKitM-1-N4
- Adafruit ESP32-C6 Feather
- Seeed Studio XIAO ESP32C6
- WeAct Studio ESP32-C6 N8
- **Waveshare ESP32-C6 1.47inch Display Development Board** (closest Waveshare C6 board supported)

### 4.2 Support for ESP32-C6-Touch-AMOLED-1.8

There is **no official CircuitPython board definition** for the ESP32-C6-Touch-AMOLED-1.8 specifically. To use CircuitPython:

1. Flash a generic ESP32-C6 CircuitPython firmware (e.g., from the ESP32-C6-DevKitM-1-N4 build)
2. The SH8601 AMOLED display has **no CircuitPython `displayio` driver**. The SH8601 is a QSPI AMOLED controller not yet in CircuitPython's display driver library.
3. The FT3168 touch controller can be used via `adafruit_focaltouch` library (compatible with FT5x06 family).

### 4.3 Limitations

- **No native USB:** The ESP32-C6 has a built-in USB Serial core for debugging but cannot act as a USB HID device (mouse, keyboard) or mass storage device.
- **SH8601 display:** Not supported by `displayio`. A custom `displayio` driver would need to be written and compiled into the firmware.
- **Recommendation:** CircuitPython is not currently a practical choice for this specific board due to the lack of SH8601 display support.

---

## 5. ESPHome

### 5.1 ESP32-C6 Support in ESPHome

ESPHome supports the ESP32-C6 variant using the **ESP-IDF framework only** (Arduino framework is not supported for ESP32-C6 in ESPHome).

### 5.2 Base Board Configuration

```yaml
esphome:
  name: waveshare-c6-amoled-18
  friendly_name: "Waveshare C6 AMOLED 1.8"
  min_version: 2025.9.0

esp32:
  board: esp32-c6-devkitc-1
  variant: esp32c6
  flash_size: 16MB
  framework:
    type: esp-idf
    version: 5.3.1
    platform_version: 6.9.0

logger:
  level: DEBUG

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome
    password: !secret ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    ssid: "C6-AMOLED-Fallback"
    password: !secret fallback_password

captive_portal:
```

### 5.3 Display Configuration (SH8601 AMOLED)

The SH8601 display driver is supported in ESPHome through the **MIPI SPI Display Driver** (`mipi_spi` platform). For the ESP32-S3 version of this board, the model `WAVESHARE-ESP32-S3-TOUCH-AMOLED-1.75` works. However, there is **no pre-defined model for the ESP32-C6-Touch-AMOLED-1.8** as of this writing.

#### Approach A: Use the Existing Waveshare S3 Model (May Require Adjustments)

Based on community testing with the ESP32-S3 variant of this board:

```yaml
spi:
  id: spi_display
  type: quad
  clk_pin: GPIO0
  data_pins: [GPIO1, GPIO2, GPIO3, GPIO4]

display:
  - platform: mipi_spi
    id: my_display
    model: WAVESHARE-ESP32-S3-TOUCH-AMOLED-1.75
    bus_mode: quad
    cs_pin: GPIO5
    dimensions:
      width: 368
      height: 448
    brightness: 200
    color_order: RGB
```

> **Warning:** The `WAVESHARE-ESP32-S3-TOUCH-AMOLED-1.75` model was designed for the S3 variant. The initialization sequence may work identically (both use SH8601), but this has not been confirmed on the C6 version. Test thoroughly.

#### Approach B: Use CUSTOM Model with SH8601 Init Sequence

```yaml
spi:
  id: spi_display
  type: quad
  clk_pin: GPIO0
  data_pins: [GPIO1, GPIO2, GPIO3, GPIO4]

display:
  - platform: mipi_spi
    id: my_display
    model: CUSTOM
    bus_mode: quad
    cs_pin: GPIO5
    dimensions:
      width: 368
      height: 448
    brightness: 200
    color_order: RGB
    init_sequence:
      # SH8601 initialization commands
      - [0x11, 0]                    # Sleep out
      - delay 120ms
      - [0x44, 0x01, 0xBF]          # Set tear scanline
      - [0x35, 0x00]                 # Tearing effect on
      - [0x53, 0x20]                 # Write control display
      - [0x2A, 0x00, 0x00, 0x01, 0x6F]  # Column address set (0-367)
      - [0x2B, 0x00, 0x00, 0x01, 0xBF]  # Row address set (0-447)
      - [0x51, 0xFF]                 # Set brightness max
      - [0x29, 0]                    # Display on
```

### 5.4 Touch Configuration

The FT3168 is compatible with the `ft5x06` platform:

```yaml
i2c:
  id: i2c_bus
  sda: GPIO8
  scl: GPIO7
  frequency: 100kHz

touchscreen:
  - platform: ft5x06
    id: my_touchscreen
    display: my_display
    interrupt_pin: GPIO15
    address: 0x38
    i2c_id: i2c_bus
```

### 5.5 Additional Sensor Components

```yaml
# QMI8658 IMU (I2C address 0x6B)
# Not natively supported in ESPHome; use custom component or external_components

# PCF85063 RTC (I2C address 0x51)
# Use the pcf85063a time platform:
time:
  - platform: pcf85063a
    id: rtc_time
    i2c_id: i2c_bus
    address: 0x51

# AXP2101 PMU (I2C address 0x34)
# Not natively supported; use custom component
```

### 5.6 Known ESPHome Limitations

1. **I2C stability:** Some ESP-IDF versions cause I2C crashes on ESP32-C6 when BLE is also active. Disable BLE if I2C issues occur.
2. **SH8601 model:** No dedicated ESPHome model exists for this specific board; the CUSTOM approach or the S3 model adaptation is needed.
3. **LVGL in ESPHome:** ESPHome has built-in LVGL support. Once the display is working, LVGL widgets can be defined directly in YAML.

---

## 6. LVGL (Light and Versatile Graphics Library)

### 6.1 LVGL Version Compatibility

Waveshare's official examples use **LVGL v8.4.0**. The display driver libraries in the demo package are written for LVGL v8 and are **not compatible with LVGL v9**.

| LVGL Version | Compatibility |
|---|---|
| v8.4.0 | Official Waveshare examples, fully tested |
| v9.x | Requires different driver code; not yet officially supported by Waveshare for this board |

### 6.2 lv_conf.h Recommended Settings

The `lv_conf.h` file is included in the Waveshare demo package. Key settings:

```c
/* Enable lv_conf.h usage */
#define LV_CONF_INCLUDE_SIMPLE  1

/* Color settings */
#define LV_COLOR_DEPTH          16      /* RGB565 for SH8601 */
#define LV_COLOR_16_SWAP        1       /* Byte-swap for SPI transfer */

/* Memory settings */
#define LV_MEM_CUSTOM           0
#define LV_MEM_SIZE             (48U * 1024U)  /* 48 KB for LVGL internal heap */
/* Note: ESP32-C6 has no PSRAM. Adjust buffer sizes accordingly. */

/* Display buffer */
/* Buffer height of 4-10 lines is recommended for ESP32-C6 (no PSRAM) */
/* Total buffer = width * buf_height * 2 bytes (RGB565) */
/* e.g., 368 * 10 * 2 = 7,360 bytes per buffer */

/* HAL settings */
#define LV_TICK_CUSTOM          1
#define LV_TICK_CUSTOM_INCLUDE  "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* Font settings */
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_FMT_TXT_LARGE   1      /* Enable large font support */
#define LV_USE_FONT_COMPRESSED   1

/* Widget enable (enable as needed) */
#define LV_USE_ARC              1
#define LV_USE_BAR              1
#define LV_USE_BTN              1
#define LV_USE_BTNMATRIX        1
#define LV_USE_CANVAS           1
#define LV_USE_CHART            1
#define LV_USE_CHECKBOX         1
#define LV_USE_DROPDOWN         1
#define LV_USE_IMG              1
#define LV_USE_LABEL            1
#define LV_USE_LINE             1
#define LV_USE_ROLLER           1
#define LV_USE_SLIDER           1
#define LV_USE_SWITCH           1
#define LV_USE_TEXTAREA         1
#define LV_USE_TABLE            1
```

### 6.3 Display Driver Integration with LVGL (Arduino)

The SH8601 display is initialized with the Arduino_GFX library's QSPI bus:

```cpp
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

// QSPI bus setup
Arduino_ESP32QSPI *bus = new Arduino_ESP32QSPI(
    LCD_CS,     // CS:   GPIO 5
    LCD_SCLK,   // SCK:  GPIO 0
    LCD_SDIO0,  // D0:   GPIO 1
    LCD_SDIO1,  // D1:   GPIO 2
    LCD_SDIO2,  // D2:   GPIO 3
    LCD_SDIO3   // D3:   GPIO 4
);

// Display object
Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus,
    -1,         // RST pin (-1 = not connected / managed by PMU)
    0,          // Rotation
    false,      // IPS
    368,        // Width
    448         // Height
);

// LVGL display buffer (adjust size for available RAM)
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[368 * 10];  // ~7.2 KB buffer
// static lv_color_t buf2[368 * 10]; // Optional second buffer

void setup() {
    gfx->begin();
    gfx->fillScreen(BLACK);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, 368 * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 368;
    disp_drv.ver_res = 448;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Touch driver init...
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    lv_disp_flush_ready(disp);
}

void loop() {
    lv_timer_handler();
    delay(5);
}
```

### 6.4 LVGL with ESP-IDF

For LVGL v9 on ESP-IDF, the SH8601 driver requires a **rounder callback** to align coordinates to even/odd boundaries:

```c
// LVGL 9 rounder callback for SH8601
void sh8601_rounder_cb(lv_event_t *e) {
    lv_area_t *area = lv_event_get_param(e);
    area->x1 = area->x1 & ~1;  // Round down to even
    area->y1 = area->y1 & ~1;
    area->x2 = area->x2 | 1;   // Round up to odd
    area->y2 = area->y2 | 1;
}
```

Configuration for ESP-IDF LVGL integration:

```c
// Display: 368x448, RGB565, QSPI @ 40 MHz
// Buffer: dual buffers recommended
// Tick period: 2ms timer
// Refresh period: 15ms (~60 FPS target)
```

### 6.5 SquareLine Studio

#### Board Support Package Status

Waveshare provides SquareLine Studio integration. The official Waveshare SquareLine Studio page is at:
https://www.waveshare.com/wiki/Waveshare_SquareLine_Studio

The `16_LVGL_Sqprj` example in the Arduino demo package is a SquareLine Studio project that demonstrates:
- QMI8658 sensor-based adaptive screen orientation
- WiFi real-time clock display
- Touch-based backlight brightness control

#### SquareLine Studio Configuration

When creating a new SquareLine Studio project for this board:

1. **Template:** Select "Arduino with TFT_eSPI" or "Arduino" template
2. **Screen resolution:** 368 x 448
3. **Color depth:** 16 bit
4. **LVGL version:** v8.x (to match Waveshare examples)

#### Export Workflow

1. Design UI in SquareLine Studio
2. Export to Arduino project (generates `libraries/` and `ui/` directories)
3. Copy exported UI files into your Arduino sketch's library folder
4. Ensure `lv_conf.h` and display driver initialization are properly configured
5. Use the partition scheme `16M Flash (3MB APP/9.9MB FATFS)` for projects with images

#### Community Board Packages

Community-maintained SquareLine Studio board packages are available at:
https://github.com/yashmulgaonkar/SquareLineStudio_boardpackages

> **Note:** A dedicated ESP32-C6-Touch-AMOLED-1.8 board package for SquareLine Studio has not been confirmed. You may need to create a custom board package or adapt an existing Waveshare AMOLED board package.

---

## Quick Reference: Framework Comparison

| Feature | Arduino IDE | PlatformIO | ESP-IDF | MicroPython | CircuitPython | ESPHome |
|---|---|---|---|---|---|---|
| **Official support** | Yes | Partial (pioarduino fork needed for Arduino) | Yes | Partial (no display driver) | No (no board def) | Partial (no SH8601 model) |
| **Display working** | Yes | Yes | Yes | Requires custom firmware | No | Experimental |
| **Touch working** | Yes | Yes | Yes | Manual I2C | Via adafruit_focaltouch | Yes (ft5x06) |
| **LVGL support** | v8.4.0 tested | v8.4.0 tested | v8/v9 | Community only | No | Built-in |
| **Difficulty** | Low | Medium | High | High | N/A | Medium |
| **Best for** | Prototyping, beginners | CI/CD, multi-env | Production, performance | Scripting (limited) | Not recommended | Home Assistant |

---

## Key Resources

### Official Waveshare

- **Documentation portal:** https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8
- **Arduino setup guide:** https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Development-Environment-Setup-Arduino
- **Product page:** https://www.waveshare.com/esp32-c6-touch-amoled-1.8.htm
- **GitHub (examples + firmware):** https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8
- **Waveshare ESP32 components (ESP Component Registry):** https://github.com/waveshareteam/Waveshare-ESP32-components
- **SquareLine Studio page:** https://www.waveshare.com/wiki/Waveshare_SquareLine_Studio

### Espressif

- **Arduino-ESP32 core:** https://github.com/espressif/arduino-esp32
- **ESP-IDF for ESP32-C6:** https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/index.html
- **SH8601 driver (ESP Component Registry):** https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_sh8601
- **ESP32_Display_Panel library:** https://github.com/esp-arduino-libs/ESP32_Display_Panel
- **SH8601 datasheet:** https://dl.espressif.com/AE/esp-iot-solution/SH8601A0_DataSheet_Preliminary_V0.0_UCS__191107_1_.pdf

### Community

- **pioarduino (PlatformIO ESP32-C6 Arduino fork):** https://github.com/pioarduino/platform-espressif32
- **MicroPython ESP32-C6 firmware:** https://micropython.org/download/ESP32_GENERIC_C6/
- **MicroPython AMOLED driver (S3 only):** https://github.com/dobodu/Lilygo-Waveshare_Amoled-Micropython
- **ESP32-C6-Touch-AMOLED-2.06 dev template (related board):** https://github.com/strnad/ESP32-C6-Touch-AMOLED-2.06-Development-Template
- **ESPHome MIPI SPI display docs:** https://esphome.io/components/display/qspi_amoled.html
- **ESPHome Waveshare C6 LCD example:** https://github.com/hugoferreira/waveshare-esp32-c6-esphome
- **SH8601 + LVGL 9 forum thread:** https://forum.lvgl.io/t/sh8601-with-lvgl-9-waveshare-esp32-s3-touch-amoled-1-8/21818
- **ESPHome ESP32-S3 AMOLED 1.8 community thread:** https://community.home-assistant.io/t/esp32-s3-1-8inch-amoled-touch/956270

---

*Last updated: 2026-04-03*
