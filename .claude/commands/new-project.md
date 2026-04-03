# Scaffold a new project

Create a new ESP-IDF project under `projects/`. Usage: /new-project <name>

Steps:

1. Create directory structure:
   - `projects/<name>/main/`
   - `projects/<name>/main/CMakeLists.txt`
   - `projects/<name>/main/main.c`
   - `projects/<name>/main/idf_component.yml`   ← component dependencies
   - `projects/<name>/CMakeLists.txt`
   - `projects/<name>/sdkconfig.defaults`        ← gitignored, user fills credentials locally
   - `projects/<name>/sdkconfig.defaults.template` ← committed, placeholder values only
   - `projects/<name>/partitions.csv`            ← custom partition (2-3MB app)
   - `projects/<name>/README.md`

2. `projects/<name>/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "../../shared/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(<name>)
```

3. `projects/<name>/main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "main.c"
                        INCLUDE_DIRS ".")
```

4. `projects/<name>/main/idf_component.yml`:
```yaml
dependencies:
  espressif/esp_lcd_sh8601: "^2.0.1"
  espressif/esp_lcd_touch_ft5x06: "*"
  espressif/esp_io_expander_tca9554: "^2.0.0"
```
   Add more dependencies as needed (XPowersLib for AXP2101, SensorLib for QMI8658, etc.)

5. `projects/<name>/sdkconfig.defaults` (local only, gitignored):
```
CONFIG_IDF_TARGET="esp32c6"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_BT_ENABLED=n
CONFIG_LV_MEM_SIZE_KILOBYTES=64
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_20=y
```
   Add WiFi credentials here if the project needs WiFi.

6. `projects/<name>/sdkconfig.defaults.template` (committed to git):
   Same as sdkconfig.defaults but with placeholder values for any secrets.

7. `projects/<name>/partitions.csv`:
```csv
# Name,     Type, SubType, Offset,   Size, Flags
nvs,        data, nvs,      0x9000,  0x6000,
factory,    0, 0,           0x10000, 3M,
flash_test, data, fat,      ,        528K,
```
   3MB app partition (16MB flash allows larger than the 2MB used on LCD-1.47 board).

8. `projects/<name>/main/main.c` — scaffold with board init sequence:
```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "<name>";

// Board init: I2C → IO Expander → Display → Touch
// See CLAUDE.md "Critical Initialization Order" for details

void app_main(void) {
    ESP_LOGI(TAG, "Starting <name>");
    // TODO: Initialize I2C bus (GPIO 7 SCL, GPIO 8 SDA)
    // TODO: Initialize TCA9554 IO expander (0x20), set P4/P5 HIGH
    // TODO: Initialize SH8601 AMOLED display via QSPI
    // TODO: Initialize FT3168 touch controller
    // TODO: Initialize LVGL with display + touch input drivers
}
```

9. After scaffolding, set target:
   `. ~/esp/esp-idf/export.sh 2>/dev/null && idf.py -C projects/<name> set-target esp32c6`

10. Update `projects/<name>/README.md` with description, build instructions, config options.
11. Add the project to the root `README.md` project table.

## Key Board Differences from LCD-1.47

- **TCA9554 IO expander must be initialized BEFORE display/touch** — it controls their power
- **SH8601 QSPI uses GPIO 0-5** (not SPI2 with separate MOSI/MISO/CLK pins)
- **Touch is new** — FT3168 on I2C 0x38, must register as LVGL input device
- **No RGB LED** — no WS2812, no led_strip component needed
- **No backlight PWM** — brightness via SH8601 register 0x51 (0-255)
- **16MB flash** — use CONFIG_ESPTOOLPY_FLASHSIZE_16MB, larger partitions possible
- **AMOLED coordinates must be even** — LVGL needs rounder callback

## LVGL Gotchas (Still Apply)

- Use `lv_color_make(r, g, b)` — NOT `LV_COLOR_MAKE(r, g, b)` in function arguments.
- All LVGL mutations must happen inside `lv_timer` callbacks, never from FreeRTOS tasks.
- `lv_spinner_create(parent, speed_ms, arc_degrees)` takes 3 arguments total.
- Arc angle 270° = top of circle (12 o'clock). Angles increase clockwise.
- ESP32-C6 is single-core — use `xTaskCreate()`, never `xTaskCreatePinnedToCore()`.
