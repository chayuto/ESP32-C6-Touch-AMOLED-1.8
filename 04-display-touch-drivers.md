# Display and Touch Controller Drivers -- Waveshare ESP32-C6-Touch-AMOLED-1.8

Research date: 2026-04-03

---

## 1. Board Overview

The Waveshare ESP32-C6-Touch-AMOLED-1.8 is a smartwatch-form-factor development board built around the ESP32-C6 MCU. It features a 1.8" AMOLED touchscreen display connected via QSPI, along with dual digital microphones, an IMU, RTC, and power management IC.

- **Product page:** https://www.waveshare.com/esp32-c6-touch-amoled-1.8.htm
- **Wiki:** https://www.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-1.8 (not yet populated as of research date)
- **Official repo:** https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8
- **License:** Apache-2.0

---

## 2. Display Controller: SH8601

### 2.1 Chip Identification

The AMOLED display uses the **SH8601** AMOLED display driver IC (manufactured by Sino Wealth). This was confirmed from:

- The official Waveshare demo code instantiating `Arduino_SH8601` in all Arduino examples.
- The ESP-IDF examples using the `esp_lcd_sh8601` component from Espressif's component registry.
- The `Arduino_GFX_Library` bundled in the repo containing the SH8601 driver.

The SH8601 is an AMOLED controller with a maximum addressable resolution of 480x480 pixels. In this board it drives a **368x448** active pixel area (the README states 368x488, but `pin_config.h` defines `LCD_WIDTH=368` and `LCD_HEIGHT=448`; the ESP-IDF example uses 368x448 as well). The init sequence sets column address range 0x0000-0x016F (0-367) and row address range 0x0000-0x01BF (0-447), confirming 368x448.

**Note on resolution discrepancy:** The GitHub README and product description state "368x488 Pixels" but all working code in the repository configures 368x448. Developers should test both values; the code-level configuration (368x448) is the proven working value.

### 2.2 Display Specifications

| Parameter | Value |
|---|---|
| Display type | AMOLED |
| Display size | 1.8 inches |
| Controller IC | SH8601 (Sino Wealth) |
| Max resolution (IC) | 480 x 480 |
| Active resolution (board) | 368 x 448 (code) / 368 x 488 (marketing) |
| Interface | QSPI (Quad SPI) |
| Color depth | 16-bit RGB565 (default), 18-bit RGB666, 24-bit RGB888 |
| QSPI clock speed | 40 MHz |
| Color order | RGB (configurable to BGR) |
| Brightness control | Hardware, via register 0x51 (0-255) |
| Contrast enhancement | Off / Low / Medium / High (Sunlight Readability Enhancement) |
| AOD (Always-On Display) | Supported by IC (registers 0x48/0x49) |
| HBM (High Brightness Mode) | Supported by IC (registers 0x63/0x66) |
| Deep standby mode | Supported (register 0x4F) |
| Tearing effect | Supported (register 0x35) |
| Inversion control | Supported |
| Backlight | Not applicable (self-emissive AMOLED, no BK_LIGHT pin) |

### 2.3 QSPI Pin Configuration

| Signal | GPIO Pin |
|---|---|
| SCLK (Clock) | GPIO 0 |
| SDIO0 / DATA0 | GPIO 1 |
| SDIO1 / DATA1 | GPIO 2 |
| SDIO2 / DATA2 | GPIO 3 |
| SDIO3 / DATA3 | GPIO 4 |
| CS (Chip Select) | GPIO 5 |
| RST (Reset) | Not connected (software reset used; controlled via IO expander) |

### 2.4 IO Expander for Display/Touch Power

The board uses a **TCA9554 / XCA9554** I2C IO expander at address **0x20** to control power rails:

- **IO Expander Pin 4:** Display power enable (set HIGH to enable)
- **IO Expander Pin 5:** Touch power enable (set HIGH to enable)

Both must be set HIGH before initializing the display or touch controller. A 200-500ms delay after power-on is recommended.

### 2.5 QSPI Protocol Details

The SH8601 QSPI protocol uses:

- **Command bits:** 8 (Arduino_GFX) or 32 (ESP-IDF, which packs opcode + command into 32 bits)
- **Parameter bits:** 8
- **SPI Mode:** 0
- **DMA channel:** `SPI_DMA_CH_AUTO` (automatic DMA channel selection)
- **SPI Host:** `SPI2_HOST`
- **Write opcode:** `0x02` (single command), `0x32` (color data)
- **Read opcode:** `0x03`
- **Max transfer size (Arduino_GFX):** 1024 pixels at once (`ESP32QSPI_MAX_PIXELS_AT_ONCE`)
- **Max transfer size (ESP-IDF):** Full framebuffer (`H_RES * V_RES * BPP / 8`)

**Important coordinate constraint:** When using `esp_lcd_panel_draw_bitmap()`, x_start, y_start, x_end, and y_end must be divisible by 2. The ESP-IDF LVGL example implements a rounder callback (`example_lvgl_rounder_cb`) to enforce this.

### 2.6 SH8601 Initialization Sequence

From the ESP-IDF example, the proven init sequence is:

```c
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},           // Sleep Out, wait 120ms
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},       // Set Tear Scan Line
    {0x35, (uint8_t[]){0x00}, 1, 0},              // Tearing Effect On
    {0x53, (uint8_t[]){0x20}, 1, 10},             // Write CTRL Display1
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0}, // Column Address Set (0-367)
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0}, // Page Address Set (0-447)
    {0x51, (uint8_t[]){0x00}, 1, 10},             // Brightness = 0
    {0x29, (uint8_t[]){0x00}, 0, 10},             // Display On
    {0x51, (uint8_t[]){0xFF}, 1, 0},              // Brightness = max
};
```

From the Arduino_GFX library, the init uses batch operations:
1. Sleep Out (0x11) + 120ms delay
2. Normal Display Mode On (0x13)
3. Inversion Off (0x20)
4. Pixel Format 16-bit (0x3A, 0x05)
5. Display On (0x29)
6. Brightness control on + dimming on (0x53, 0x28)
7. Brightness value (0x51, 0xD0)
8. Contrast enhancement off (0x58, 0x00)

### 2.7 Key SH8601 Register Map

| Register | Name | Description |
|---|---|---|
| 0x01 | SWRESET | Software Reset |
| 0x10 | SLPIN | Sleep In |
| 0x11 | SLPOUT | Sleep Out |
| 0x13 | NORON | Normal Display Mode On |
| 0x20 | INVOFF | Inversion Off |
| 0x21 | INVON | Inversion On |
| 0x28 | DISPOFF | Display Off |
| 0x29 | DISPON | Display On |
| 0x2A | CASET | Column Address Set |
| 0x2B | PASET | Page Address Set |
| 0x2C | RAMWR | Memory Write Start |
| 0x35 | TEARON | Tearing Effect On |
| 0x36 | MADCTL | Memory Data Access Control |
| 0x3A | PIXFMT | Pixel Format (0x05=16bit, 0x06=18bit, 0x07=24bit) |
| 0x48 | AODMOFF | AOD Mode Off |
| 0x49 | AODMON | AOD Mode On |
| 0x4F | DEEPSTMODE | Deep Standby Mode On |
| 0x51 | WDBRIGHTNESSVALNOR | Write Brightness (Normal Mode) |
| 0x53 | WCTRLD1 | Write CTRL Display1 (brightness/dimming control) |
| 0x55 | WCTRLD2 | Write CTRL Display2 |
| 0x58 | WCE | Write Contrast Enhancement |
| 0x63 | WDBRIGHTNESSVALHBM | Write Brightness (HBM Mode) |
| 0x66 | WHBMCTL | Write HBM Control |
| 0xC4 | SPIMODECTL | SPI Mode Control |

---

## 3. Touch Controller: FT3168

### 3.1 Chip Identification

The capacitive touch controller is the **FT3168** from FocalTech. This was confirmed from:

- Arduino examples explicitly naming it `FT3168` and using `Arduino_FT3x68` class.
- ESP-IDF examples using the `esp_lcd_touch_ft5x06` component (the FT3168 is register-compatible with the FT5x06 family).
- The FT3168 device address is defined as `FT3168_DEVICE_ADDRESS = 0x38`.

The FT3168 is part of FocalTech's FT3x68/FT5x06 family of single-chip capacitive touch controllers. The Device ID register (0xA0) returns `0x03` for FT3168.

### 3.2 Touch Controller Specifications

| Parameter | Value |
|---|---|
| Controller IC | FT3168 (FocalTech) |
| Interface | I2C |
| I2C Address | 0x38 |
| Multi-touch | Up to 2 touch points |
| Interrupt | Active low pulse on touch detection |
| Gesture recognition | Swipe Left/Right/Up/Down, Double Click |
| Power modes | Active, Monitor, Standby, Hibernate |
| Proximity sensing | Supported |

### 3.3 Touch I2C Pin Configuration

| Signal | GPIO Pin |
|---|---|
| SDA | GPIO 8 |
| SCL | GPIO 7 |
| INT (Interrupt) | GPIO 15 |
| RST (Reset) | Not connected (controlled via IO expander) |

The I2C bus at address 0x20 (TCA9554 IO expander) controls touch power via pin 5.

### 3.4 FT3168 Register Map (Key Registers)

| Register | Name | R/W | Description |
|---|---|---|---|
| 0x02 | FINGERNUM | R | Number of fingers currently touching (0, 1, or 2) |
| 0x03 | X1POSH | R | Touch 1 X position high nibble (bits 11:8) |
| 0x04 | X1POSL | R | Touch 1 X position low byte (bits 7:0) |
| 0x05 | Y1POSH | R | Touch 1 Y position high nibble (bits 11:8) |
| 0x06 | Y1POSL | R | Touch 1 Y position low byte (bits 7:0) |
| 0x09 | X2POSH | R | Touch 2 X position high nibble |
| 0x0A | X2POSL | R | Touch 2 X position low byte |
| 0x0B | Y2POSH | R | Touch 2 Y position high nibble |
| 0x0C | Y2POSL | R | Touch 2 Y position low byte |
| 0xA0 | DEVICE_ID | R | Device ID (0x03 = FT3168) |
| 0xA5 | POWER_MODE | R/W | Power mode (0=Active, 1=Monitor, 2=Standby, 3=Hibernate) |
| 0xB0 | PROX_SENSE | R/W | Proximity sensing mode (0=Off, 1=On) |
| 0xD0 | GESTUREID_MODE | R/W | Gesture ID mode (0=Off, 1=On) |
| 0xD3 | GESTUREID | R | Gesture ID result |

### 3.5 Gesture IDs

| ID | Gesture |
|---|---|
| 0x00 | No Gesture |
| 0x20 | Swipe Left |
| 0x21 | Swipe Right |
| 0x22 | Swipe Up |
| 0x23 | Swipe Down |
| 0x24 | Double Click |

### 3.6 Reading Touch Coordinates

Touch X/Y coordinates are 12-bit values. Reading requires combining two registers:

```c
// X coordinate
uint8_t x_high, x_low;
i2c_read(0x38, 0x03, &x_high);  // High nibble (mask with 0x0F)
i2c_read(0x38, 0x04, &x_low);   // Low byte
int16_t x = ((x_high & 0x0F) << 8) | x_low;

// Y coordinate
uint8_t y_high, y_low;
i2c_read(0x38, 0x05, &y_high);
i2c_read(0x38, 0x06, &y_low);
int16_t y = ((y_high & 0x0F) << 8) | y_low;
```

---

## 4. Driver Libraries

### 4.1 Arduino_GFX (GFX Library for Arduino)

**Status: Fully supported (primary library used by Waveshare)**

The board's official examples use a bundled fork of Arduino_GFX v1.6.4 by moononournation. The upstream Arduino_GFX library (`https://github.com/moononournation/Arduino_GFX`) also includes the `Arduino_SH8601` driver natively.

**Key classes used:**
- `Arduino_ESP32QSPI` -- QSPI data bus for ESP32 family (including ESP32-C6)
- `Arduino_SH8601` -- Display driver (inherits from `Arduino_OLED`)

**Instantiation pattern:**

```cpp
#include "Arduino_GFX_Library.h"

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    5 /* CS */, 0 /* SCK */, 1 /* SDIO0 */, 2 /* SDIO1 */,
    3 /* SDIO2 */, 4 /* SDIO3 */);

Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */,
    368 /* width */, 448 /* height */);
```

**Features:**
- Hardware QSPI at 40 MHz (default)
- DMA-capable buffer allocation via `heap_caps_aligned_alloc(16, size, MALLOC_CAP_DMA)`
- Double-buffered DMA transfers (two buffers of 1024 pixels each)
- 16-bit RGB565 default pixel format
- Brightness control (0-255) via `gfx->setBrightness()`
- Contrast enhancement via `gfx->setContrast()`
- Software rotation support

### 4.2 Arduino_DriveBus (Touch Library)

**Status: Used for touch in official Waveshare examples**

The `Arduino_DriveBus` library provides the `Arduino_FT3x68` class for the FT3168 touch controller.

**Instantiation pattern:**

```cpp
#include "Arduino_DriveBus_Library.h"

std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
    std::make_shared<Arduino_HWIIC>(8 /* SDA */, 7 /* SCL */, &Wire);

void Arduino_IIC_Touch_Interrupt(void);

std::unique_ptr<Arduino_IIC> FT3168(
    new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS,  // 0x38
                       DRIVEBUS_DEFAULT_VALUE, 15 /* INT */,
                       Arduino_IIC_Touch_Interrupt));

void Arduino_IIC_Touch_Interrupt(void) {
    FT3168->IIC_Interrupt_Flag = true;
}
```

### 4.3 ESP-IDF esp_lcd_sh8601 Component

**Status: Fully supported**

- **Component:** `espressif/esp_lcd_sh8601` v2.0.1
- **Install:** `idf.py add-dependency "espressif/esp_lcd_sh8601^2.0.1"`
- **Source:** https://components.espressif.com/components/espressif/esp_lcd_sh8601

Provides `esp_lcd_new_panel_sh8601()` which creates an `esp_lcd_panel_handle_t` for use with ESP-IDF's LCD panel API. Supports both SPI and QSPI modes via the `sh8601_vendor_config_t` struct.

**Configuration macros provided:**
- `SH8601_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_trans_sz)` -- SPI bus config
- `SH8601_PANEL_IO_QSPI_CONFIG(cs, callback, ctx)` -- Panel IO config (40 MHz, quad mode)

### 4.4 ESP-IDF esp_lcd_touch_ft5x06 Component

**Status: Fully supported (FT3168 is FT5x06-compatible)**

- **Component:** `espressif/esp_lcd_touch_ft5x06`
- **I2C address:** `ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS` (0x38)
- **Install:** `idf.py add-dependency "espressif/esp_lcd_touch_ft5x06"`

Works with the FT3168 because it uses the same register interface as the FT5x06 family.

### 4.5 LovyanGFX

**Status: Supported via Panel_SH8601Z**

LovyanGFX includes the `Panel_SH8601Z` driver (`src/lgfx/v1/panel/Panel_SH8601Z.hpp`), which is compatible with the SH8601. The SH8601Z is a variant in the same family.

**Features in LovyanGFX driver:**
- QSPI interface support (`start_qspi()` / `end_qspi()`)
- DMA pixel writing (`writePixels` and `writeImage` with `use_dma` parameter)
- Variable color depth (`setColorDepth()`)
- Rotation support
- Sleep/power save modes
- Hardware brightness control
- Pixel readback support

**Estimated configuration for this board (untested, derived from known hardware):**

```cpp
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_SH8601Z _panel_instance;
    lgfx::Bus_QSPI _bus_instance;
    lgfx::Touch_FT5x06 _touch_instance;  // FT3168 compatible

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.freq_write = 40000000;
            cfg.pin_sclk = 0;
            cfg.pin_io0  = 1;
            cfg.pin_io1  = 2;
            cfg.pin_io2  = 3;
            cfg.pin_io3  = 4;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs   = 5;
            cfg.pin_rst  = -1;
            cfg.panel_width  = 368;
            cfg.panel_height = 448;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _touch_instance.config();
            cfg.i2c_port = 0;
            cfg.i2c_addr = 0x38;
            cfg.pin_sda  = 8;
            cfg.pin_scl  = 7;
            cfg.pin_int  = 15;
            cfg.freq     = 200000;
            cfg.x_min    = 0;
            cfg.x_max    = 367;
            cfg.y_min    = 0;
            cfg.y_max    = 447;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
        setPanel(&_panel_instance);
    }
};
```

### 4.6 TFT_eSPI

**Status: NOT supported**

TFT_eSPI by Bodmer does not include an SH8601 driver. The SH8601 is an AMOLED controller with a QSPI interface, which is outside TFT_eSPI's standard SPI-based TFT driver architecture.

### 4.7 LVGL Integration

**Status: Fully supported (both Arduino and ESP-IDF)**

The official repo includes multiple LVGL examples:

**Arduino LVGL setup (v8.x):**

```cpp
#include <lvgl.h>
#include "lv_conf.h"

// Buffer allocation (1/4 screen, double-buffered, DMA-capable)
lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(
    screenWidth * screenHeight / 4 * sizeof(lv_color_t), MALLOC_CAP_DMA);
lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(
    screenWidth * screenHeight / 4 * sizeof(lv_color_t), MALLOC_CAP_DMA);

lv_disp_draw_buf_init(&draw_buf, buf1, buf2, screenWidth * screenHeight / 4);

// Display driver
static lv_disp_drv_t disp_drv;
lv_disp_drv_init(&disp_drv);
disp_drv.hor_res = 368;
disp_drv.ver_res = 448;
disp_drv.flush_cb = my_disp_flush;
disp_drv.draw_buf = &draw_buf;
disp_drv.sw_rotate = 1;  // Enable software rotation
lv_disp_drv_register(&disp_drv);

// Touch input driver
static lv_indev_drv_t indev_drv;
lv_indev_drv_init(&indev_drv);
indev_drv.type = LV_INDEV_TYPE_POINTER;
indev_drv.read_cb = my_touchpad_read;
lv_indev_drv_register(&indev_drv);
```

**Key LVGL configuration values** (from `lv_conf.h`):

| Setting | Value |
|---|---|
| `LV_COLOR_DEPTH` | 16 (RGB565) |
| `LV_COLOR_16_SWAP` | 0 |
| `LV_MEM_SIZE` | 48 KB |
| `LV_DISP_DEF_REFR_PERIOD` | 10 ms |
| `LV_INDEV_DEF_READ_PERIOD` | 10 ms |
| `LV_DPI_DEF` | 130 |

**ESP-IDF LVGL setup** additionally uses:
- Rounder callback to ensure coordinates are even (required by SH8601)
- `esp_timer`-based tick source at 2ms period
- FreeRTOS task for LVGL handler with mutex protection
- LVGL buffer height = `V_RES / 4` = 112 rows

---

## 5. DMA and Hardware Acceleration

### 5.1 SPI DMA on ESP32-C6

The ESP32-C6 supports SPI DMA transfers, which are utilized by both the Arduino_GFX and ESP-IDF drivers:

- **DMA channel:** `SPI_DMA_CH_AUTO` (auto-selected)
- **Buffer allocation:** `heap_caps_malloc(..., MALLOC_CAP_DMA)` or `heap_caps_aligned_alloc(16, ..., MALLOC_CAP_DMA)`
- **Arduino_GFX:** Uses two 1024-pixel (2048-byte) DMA buffers for double-buffered transfers
- **ESP-IDF:** Uses two LVGL draw buffers of `H_RES * (V_RES/4)` pixels each, allocated from DMA-capable memory

### 5.2 ESP32-C6 Limitations

The ESP32-C6 has important limitations compared to ESP32-S3 for display applications:

- **No PSRAM:** The ESP32-C6 does not support external PSRAM, so all framebuffers must fit in internal SRAM (~512 KB total, ~320 KB usable)
- **Single-core RISC-V:** Only one core at 160 MHz, no hardware parallel processing
- **No dedicated LCD peripheral:** Unlike ESP32-S3, there is no RGB parallel LCD controller
- **No 2D DMA (PPA):** No hardware pixel format conversion or rotation acceleration
- **SPI speed:** Maximum QSPI clock is 40 MHz in the reference configuration

### 5.3 Frame Rate Considerations

With the QSPI interface at 40 MHz and 16-bit color depth:
- Theoretical max throughput: 40 MHz x 4 (quad) / 16 bits = 10 Mpixels/s
- Full frame pixels: 368 x 448 = 164,864 pixels
- Theoretical max FPS: ~60 fps (not accounting for command overhead, SPI protocol overhead, CPU processing)
- Practical FPS with LVGL: 15-30 fps depending on scene complexity and partial updates
- LVGL refresh period configured at 10ms (100 Hz target), though actual frame delivery depends on flush speed

### 5.4 Memory Budget for Display Buffers

Full framebuffer at RGB565: 368 x 448 x 2 = 329,728 bytes (322 KB) -- too large for ESP32-C6's internal SRAM.

Recommended approach (used in official examples):
- **1/4 screen double buffer:** 2 x (368 x 112 x 2) = 164,864 bytes (~161 KB) for LVGL
- **1/10 screen single buffer:** 368 x 45 x 2 = 33,120 bytes (~32 KB) minimum for constrained applications

---

## 6. Other On-Board Peripherals

### 6.1 I2C Bus Devices

All I2C devices share the same bus (GPIO 8 = SDA, GPIO 7 = SCL):

| Device | I2C Address | Description |
|---|---|---|
| TCA9554 / XCA9554 | 0x20 | IO Expander (display/touch power control) |
| FT3168 | 0x38 | Touch controller |
| AXP2101 | (varies) | Power management IC |
| PCF85063 | (varies) | Real-time clock |
| QMI8658 | 0x6A or 0x6B | 6-axis IMU (accelerometer + gyroscope) |

### 6.2 Power Management (AXP2101)

Controlled via `XPowersLib` library. Manages power rails for display, touch, and other peripherals.

### 6.3 IMU (QMI8658)

6-axis IMU accessible via `SensorLib` (`SensorQMI8658`). The LVGL Widgets example demonstrates using accelerometer data for automatic screen rotation.

### 6.4 Audio (ES8311)

Dual digital microphone array with ES8311 audio codec:

| Signal | GPIO Pin |
|---|---|
| I2S_MCK | GPIO 19 |
| I2S_BCK | GPIO 20 |
| I2S_DI | GPIO 21 |
| I2S_WS | GPIO 22 |
| I2S_DO | GPIO 23 |

### 6.5 SD Card (SPI/SDMMC)

| Signal | GPIO Pin |
|---|---|
| SDMMC_CLK | GPIO 11 |
| SDMMC_CMD | GPIO 10 |
| SDMMC_DATA | GPIO 18 |
| SDMMC_CS | GPIO 6 |

---

## 7. Complete Pin Map

| GPIO | Function |
|---|---|
| GPIO 0 | LCD SCLK |
| GPIO 1 | LCD SDIO0 |
| GPIO 2 | LCD SDIO1 |
| GPIO 3 | LCD SDIO2 |
| GPIO 4 | LCD SDIO3 |
| GPIO 5 | LCD CS |
| GPIO 6 | SD Card CS |
| GPIO 7 | I2C SCL |
| GPIO 8 | I2C SDA |
| GPIO 10 | SD Card CMD |
| GPIO 11 | SD Card CLK |
| GPIO 15 | Touch INT |
| GPIO 18 | SD Card DATA |
| GPIO 19 | I2S MCK |
| GPIO 20 | I2S BCK |
| GPIO 21 | I2S DI |
| GPIO 22 | I2S WS |
| GPIO 23 | I2S DO |

---

## 8. Official Code Examples

The official repository provides examples for both Arduino (v3.3.5) and ESP-IDF (v5.5.1):

### 8.1 Arduino Examples

| Example | Description |
|---|---|
| 01_HelloWorld | Basic display initialization and text rendering |
| 02_Drawing_board | Touch-based drawing application |
| 03_GFX_AsciiTable | ASCII character display |
| 04_GFX_FT3168_Image | Touch-triggered image display |
| 05_GFX_PCF85063_simpleTime | RTC time display |
| 06_GFX_ESPWiFiAnalyzer | WiFi analyzer visualization |
| 07_GFX_Clock | Analog clock face |
| 08_LVGL_Animation | LVGL animation demo with SquareLine Studio UI |
| 09_LVGL_change_background | LVGL background color switching |
| 10_LVGL_PCF85063_simpleTime | LVGL-based RTC display |
| 11_LVGL_QMI8658_ui | LVGL with IMU data display |
| 13_LVGL_Widgets | LVGL widget demo with auto-rotation via IMU |
| 15_ES8311 | Audio codec demo |
| 16_LVGL_Sqprj | SquareLine Studio project |

### 8.2 ESP-IDF Examples

| Example | Description |
|---|---|
| 01_AXP2101 | Power management IC demo |
| 02_PCF85063 | RTC demo |
| 03_esp-brookesia | ESP-Brookesia UI framework demo |
| 04_QMI8658 | IMU demo |
| 05_LVGL_WITH_RAM | LVGL with DMA buffers (music player demo) |

### 8.3 Required Arduino Libraries

The following libraries are bundled in the repo under `examples/Arduino-v3.3.5/libraries/`:

- `GFX_Library_for_Arduino` v1.6.4 -- Display driver (Arduino_GFX fork with SH8601)
- `Arduino_DriveBus` -- Touch driver (FT3x68) and hardware I2C abstraction
- `Adafruit_BusIO` -- I2C/SPI abstraction
- `Adafruit_XCA9554` -- IO expander driver
- `XPowersLib` -- AXP2101 power management
- `SensorLib` -- QMI8658 IMU driver
- `lvgl` v8.x -- LVGL graphics library
- `ui_a`, `ui_b`, `ui_c` -- Pre-built SquareLine Studio UI assets

---

## 9. Quick Start Initialization Sequence

For any project using this board, the following initialization order is required:

```
1. Initialize I2C bus (GPIO 8 = SDA, GPIO 7 = SCL)
2. Initialize IO expander at 0x20
3. Set IO expander pin 4 HIGH (display power)
4. Set IO expander pin 5 HIGH (touch power)
5. Wait 200-500ms for power stabilization
6. Initialize QSPI bus and SH8601 display
7. Initialize FT3168 touch controller
8. Set display brightness
```

**Minimal Arduino sketch structure:**

```cpp
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include <Adafruit_XCA9554.h>

Adafruit_XCA9554 expander;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(5, 0, 1, 2, 3, 4);
Arduino_SH8601 *gfx = new Arduino_SH8601(bus, -1, 0, 368, 448);

std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
    std::make_shared<Arduino_HWIIC>(8, 7, &Wire);
void touch_isr(void);
std::unique_ptr<Arduino_IIC> touch(
    new Arduino_FT3x68(IIC_Bus, 0x38, -1, 15, touch_isr));
void touch_isr(void) { touch->IIC_Interrupt_Flag = true; }

void setup() {
    Wire.begin(8, 7);

    expander.begin(0x20);
    expander.pinMode(4, OUTPUT);
    expander.pinMode(5, OUTPUT);
    expander.digitalWrite(4, HIGH);
    expander.digitalWrite(5, HIGH);
    delay(500);

    touch->begin();
    gfx->begin();
    gfx->fillScreen(RGB565_BLACK);
    gfx->setBrightness(200);
}
```

---

## 10. Library Compatibility Summary

| Library | Display (SH8601) | Touch (FT3168) | Notes |
|---|---|---|---|
| Arduino_GFX | Yes (native) | -- | Primary library. QSPI + DMA. |
| Arduino_DriveBus | -- | Yes (native) | Arduino_FT3x68 class. |
| ESP-IDF esp_lcd_sh8601 | Yes (v2.0.1) | -- | Espressif component registry. |
| ESP-IDF esp_lcd_touch_ft5x06 | -- | Yes | FT3168 is FT5x06-compatible. |
| LovyanGFX | Yes (Panel_SH8601Z) | Yes (Touch_FT5x06) | QSPI + DMA + touch in one library. |
| TFT_eSPI | No | -- | No SH8601 / QSPI AMOLED support. |
| LVGL | Yes (via flush callback) | Yes (via input callback) | v8.x confirmed working. |

---

## 11. Sources

- Official Waveshare GitHub repo: https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8
- Espressif esp_lcd_sh8601 component: https://components.espressif.com/components/espressif/esp_lcd_sh8601
- Espressif esp_lcd_touch_ft5x06 component: https://components.espressif.com/components/espressif/esp_lcd_touch_ft5x06
- Arduino_GFX upstream: https://github.com/moononournation/Arduino_GFX
- LovyanGFX Panel_SH8601Z: https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx/v1/panel/Panel_SH8601Z.hpp
