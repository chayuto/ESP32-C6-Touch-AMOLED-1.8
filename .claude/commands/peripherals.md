# I2C Peripheral Reference

Quick reference for all onboard I2C peripherals on the ESP32-C6-Touch-AMOLED-1.8.
Use this when initializing, reading, or configuring any onboard sensor/IC.

All devices share one I2C bus: **GPIO 7 (SCL), GPIO 8 (SDA), 200kHz**.

---

## TCA9554 IO Expander (0x20) — Init FIRST

Controls power to display and touch. Must be configured before either will work.

```c
// ESP-IDF:
#include "esp_io_expander_tca9554.h"
esp_io_expander_handle_t io_expander;
esp_io_expander_new_i2c_tca9554(I2C_NUM_0, 0x20, &io_expander);
esp_io_expander_set_dir(io_expander, IO_EXPANDER_PIN_NUM_4 | IO_EXPANDER_PIN_NUM_5, IO_EXPANDER_OUTPUT);
esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_4, 0);
esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_5, 0);
vTaskDelay(pdMS_TO_TICKS(200));
esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_4, 1);  // Display ON
esp_io_expander_set_level(io_expander, IO_EXPANDER_PIN_NUM_5, 1);  // Touch ON

// Arduino:
#include <Adafruit_XCA9554.h>
Adafruit_XCA9554 expander;
expander.begin(0x20);
expander.pinMode(4, OUTPUT);
expander.pinMode(5, OUTPUT);
expander.digitalWrite(4, HIGH);
expander.digitalWrite(5, HIGH);
delay(500);
```

| Pin | Function |
|-----|----------|
| P4 | AMOLED display power (HIGH=ON) |
| P5 | Touch controller power (HIGH=ON) |
| P0-P3, P6-P7 | Unknown — check schematic for potential use |

---

## AXP2101 PMIC (0x34)

Power management: battery charge/discharge, voltage rails, power button, fuel gauge.

### Arduino (XPowersLib)
```cpp
#include <XPowersLib.h>
XPowersPMU pmu;
pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);

// Battery info
float vbat = pmu.getBattVoltage();    // mV
int percent = pmu.getBatteryPercent(); // 0-100
bool charging = pmu.isCharging();

// Configure charging
pmu.setChargerConstantCurr(200);      // mA
pmu.setChargeTargetVoltage(4200);     // mV

// Power rails
pmu.enableALDO1();
pmu.setALDO1Voltage(3300);            // mV

// Power button IRQ
pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
```

### Key power outputs
| Rail | Range | Max Current | Typical Use |
|------|-------|-------------|-------------|
| DCDC1 | 1.5-3.4V | 2000mA | ESP32-C6 core 3.3V |
| ALDO1-4 | 0.5-3.5V | 300mA ea | Peripheral power |
| BLDO1 | 0.5-3.5V | 300mA | Display power |
| BLDO2 | 0.5-3.5V | 300mA | Microphone power |

### Deep sleep
Turn off unnecessary rails before ESP32 deep sleep:
```cpp
pmu.disableALDO1();   // Turn off unused peripherals
pmu.disableBLDO2();   // Turn off mic
// Keep DCDC1 (ESP32 core) and BLDO1 (display for AOD) if needed
```

---

## QMI8658 IMU (0x6B)

6-axis accelerometer + gyroscope. Use for auto-rotation, gesture, pedometer.

### Arduino (SensorLib)
```cpp
#include "SensorQMI8658.hpp"
SensorQMI8658 qmi;
qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);  // 0x6B

qmi.configAccelerometer(
    SensorQMI8658::ACC_RANGE_4G,
    SensorQMI8658::ACC_ODR_1000Hz,
    SensorQMI8658::LPF_MODE_0);
qmi.enableAccelerometer();

qmi.configGyroscope(
    SensorQMI8658::GYR_RANGE_512DPS,
    SensorQMI8658::GYR_ODR_896_8Hz,
    SensorQMI8658::LPF_MODE_3);
qmi.enableGyroscope();

// Reading data:
IMUdata acc, gyr;
if (qmi.getDataReady()) {
    qmi.getAccelerometer(acc.x, acc.y, acc.z);
    qmi.getGyroscope(gyr.x, gyr.y, gyr.z);
    float temp = qmi.getTemperature_C();
}
```

### Accelerometer ranges
| Range | Sensitivity | Use case |
|-------|-------------|----------|
| ±2g | 16384 LSB/g | High precision, slow motion |
| ±4g | 8192 LSB/g | **Default** — general purpose |
| ±8g | 4096 LSB/g | Activity tracking |
| ±16g | 2048 LSB/g | Impact detection |

### Auto-rotation for display
```c
// Simple orientation from accelerometer:
// acc.x > threshold → landscape left
// acc.x < -threshold → landscape right
// acc.y > threshold → portrait normal
// acc.y < -threshold → portrait inverted
```

---

## PCF85063 RTC (0x51)

Real-time clock with alarm and backup battery support.

### Arduino (SensorLib)
```cpp
#include "SensorPCF85063.hpp"
SensorPCF85063 rtc;
rtc.begin(Wire, PCF85063_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);  // 0x51

// Set time
rtc.setDateTime(2026, 4, 3, 14, 30, 0);  // year, month, day, hour, min, sec

// Read time
RTC_DateTime datetime = rtc.getDateTime();
printf("%04d-%02d-%02d %02d:%02d:%02d\n",
    datetime.year, datetime.month, datetime.day,
    datetime.hour, datetime.minute, datetime.second);

// Set alarm (wake at 7:00 AM)
rtc.setAlarmByMinutes(0);
rtc.setAlarmByHours(7);
rtc.enableAlarm();
```

### Key registers
| Addr | Register | Description |
|------|----------|-------------|
| 0x00 | Control_1 | STOP, EXT_TEST, clock control |
| 0x01 | Control_2 | AIE (alarm IE), TIE (timer IE), AF, TF |
| 0x04-0x0A | Time | Seconds through Years (BCD) |
| 0x0B-0x0F | Alarm | Second through Weekday alarm |
| 0x10-0x11 | Timer | Countdown timer value + mode |

### Deep sleep wakeup
PCF85063 alarm INT pin can wake ESP32-C6 from deep sleep (if routed to RTC GPIO).
Check schematic for INT pin connection.

---

## ES8311 Audio Codec (0x18)

Low-power mono codec for microphone input and speaker output.

### I2S Pin Connections
```
GPIO 19 → MCLK  (Master Clock, 256×Fs)
GPIO 20 → BCLK  (Bit Clock)
GPIO 22 → WS    (Word Select / LRCK)
GPIO 21 → DIN   (Mic ADC data → ESP32)
GPIO 23 → DOUT  (ESP32 → Speaker DAC)
```

### ESP-IDF Setup
```c
#include "es8311.h"

// I2C control init (already done if I2C bus is shared)
es8311_handle_t codec = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);  // 0x18
es8311_init(codec, &codec_cfg);

// I2S data path
i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);

i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),  // 16kHz for voice
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
        .mclk = 19, .bclk = 20, .ws = 22,
        .dout = 23, .din = 21,
    },
};
```

### Sample rates for different use cases
| Rate | Use case |
|------|----------|
| 8 kHz | Telephony, basic voice commands |
| 16 kHz | Voice recognition, AI assistant (XiaoZhi) |
| 44.1 kHz | Music playback |
| 48 kHz | High-quality audio |

### XiaoZhi AI Integration
This board supports the XiaoZhi AI framework (v2.2.4+):
- Wake word detection → Record via dual mic → Send to cloud LLM → Play response
- Supports DeepSeek, Doubao, and other LLM platforms
- Dual microphone array enables noise cancellation

---

## FT3168 Touch Controller (0x38)

See `/display-ui` for LVGL integration. Key details here for direct register access.

### Gesture reading (bypass LVGL)
```c
uint8_t gesture_id;
i2c_master_read_from_device(I2C_NUM_0, 0x38, 0xD3, &gesture_id, 1);
// 0x20=Left, 0x21=Right, 0x22=Up, 0x23=Down, 0x24=DoubleClick
```

### Power modes
```c
uint8_t mode;
// 0x00 = Active (default)
// 0x01 = Monitor (low power, wakes on touch)
// 0x02 = Standby
// 0x03 = Hibernate (lowest power, must reset to wake)
i2c_master_write_to_device(I2C_NUM_0, 0x38, (uint8_t[]){0xA5, mode}, 2);
```

---

## Initialization Order Summary

```
1. i2c_param_config() + i2c_driver_install()  // I2C bus
2. TCA9554 (0x20) → P4=HIGH, P5=HIGH          // Power up display + touch
3. AXP2101 (0x34) → Configure power rails      // Optional: battery, voltage config
4. SH8601 display init (QSPI)                  // Display on
5. FT3168 (0x38) → Touch init                  // Touch ready
6. QMI8658 (0x6B) → IMU init                   // Motion sensing
7. PCF85063 (0x51) → RTC init                  // Time keeping
8. ES8311 (0x18) + I2S init                    // Audio ready
```
