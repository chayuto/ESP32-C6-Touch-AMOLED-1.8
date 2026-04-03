# I2C Bus Topology & Onboard Peripherals

All peripherals share a single I2C bus: **GPIO 7 (SCL)** + **GPIO 8 (SDA)**

---

## 1. I2C Bus Map

| Device | I2C Address | Type | Function |
|--------|-------------|------|----------|
| **TCA9554** (XCA9554) | **0x20** | IO Expander | Controls display/touch power rails |
| **AXP2101** | **0x34** | PMIC | Battery charge/discharge, power rails |
| **FT3168** | **0x38** | Touch Controller | Capacitive touch input |
| **PCF85063** | **0x51** | RTC | Real-time clock with backup battery |
| **QMI8658** | **0x6B** | IMU | 6-axis accelerometer + gyroscope |
| **ES8311** | **0x18** | Audio Codec | I2C control bus (audio data via I2S) |

**Total: 6 devices on one I2C bus**

### I2C Bus Configuration
- Clock speed: **200 kHz** (from ESP-IDF example `i2c_conf`)
- Pull-ups: Internal GPIO pull-ups enabled (`GPIO_PULLUP_ENABLE`)
- Bus capacity: 6 devices is within I2C spec but near practical limit for capacitance

### Adding External I2C Devices
- The I2C bus is exposed on expansion pads (1.5mm pitch)
- External devices CAN be added if they don't conflict with addresses above
- **Avoid addresses:** 0x18, 0x20, 0x34, 0x38, 0x51, 0x6B
- Consider bus capacitance with long wires to external devices
- May need to reduce clock speed for long I2C cables

---

## 2. TCA9554 / XCA9554 IO Expander (0x20)

### Overview
- Address: **0x20** (A0=A1=A2=GND → `ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000`)
- 8 configurable I/O pins (P0-P7)
- Power-on default: All pins are HIGH-Z inputs
- Must configure as outputs and set HIGH before display/touch will work

### Pin Assignments (Confirmed from ESP-IDF Example)

| Pin | Direction | Function | Required State |
|-----|-----------|----------|----------------|
| P0 | Unknown | Likely unused or connected to other peripheral | TBD |
| P1 | Unknown | Likely unused or connected to other peripheral | TBD |
| P2 | Unknown | Likely unused or connected to other peripheral | TBD |
| P3 | Unknown | Likely unused or connected to other peripheral | TBD |
| **P4** | **OUTPUT** | **AMOLED display power enable** | **HIGH = ON** |
| **P5** | **OUTPUT** | **Touch controller power enable** | **HIGH = ON** |
| P6 | Unknown | Likely unused or connected to other peripheral | TBD |
| P7 | Unknown | Likely unused or connected to other peripheral | TBD |

### Critical Initialization Sequence

```c
// ESP-IDF pattern:
esp_io_expander_new_i2c_tca9554(I2C_HOST, 0x20, &io_expander);
esp_io_expander_set_dir(io_expander, PIN4 | PIN5, OUTPUT);

// Reset sequence: LOW first, then HIGH after 200ms
esp_io_expander_set_level(io_expander, PIN4, 0);  // Display power OFF
esp_io_expander_set_level(io_expander, PIN5, 0);  // Touch power OFF
vTaskDelay(pdMS_TO_TICKS(200));
esp_io_expander_set_level(io_expander, PIN4, 1);  // Display power ON
esp_io_expander_set_level(io_expander, PIN5, 1);  // Touch power ON

// Arduino pattern:
Adafruit_XCA9554 expander;
expander.begin(0x20);
expander.pinMode(4, OUTPUT);
expander.pinMode(5, OUTPUT);
expander.digitalWrite(4, HIGH);
expander.digitalWrite(5, HIGH);
delay(500);
```

### ESP-IDF Component
- `espressif/esp_io_expander_tca9554` v2.0.x
- Install: `idf.py add-dependency "espressif/esp_io_expander_tca9554"`

### TCA9554 Register Map
| Register | Address | Description |
|----------|---------|-------------|
| Input Port | 0x00 | Read pin states |
| Output Port | 0x01 | Write pin states |
| Polarity Inversion | 0x02 | Invert input polarity |
| Configuration | 0x03 | Direction (0=output, 1=input) |

---

## 3. AXP2101 PMIC (0x34)

### Overview
- I2C Address: **0x34**
- Manufacturer: X-Powers (Shenzhen)
- Input voltage: 2.5V - 5.5V (USB or battery)
- Supports Li-ion / Li-polymer battery charge and discharge

### Power Output Rails

| Rail | Type | Voltage Range | Step | Max Current | Likely Powers |
|------|------|--------------|------|-------------|---------------|
| DCDC1 | Buck | 1.5-3.4V | 100mV | 2000mA | ESP32-C6 core (3.3V) |
| DCDC2 | Buck | 0.5-1.2V / 1.22-1.54V | 10mV/20mV | 2000mA | Unused or low-voltage rail |
| DCDC3 | Buck | 0.5-1.2V / 1.22-1.54V / 1.6-3.4V | varies | 2000mA | Likely 3.3V peripherals |
| DCDC4 | Buck | 0.5-1.2V / 1.22-1.84V | varies | 1500mA | IO voltage |
| DCDC5 | Buck | 1.4-3.7V | 100mV | 1000mA | Likely unused |
| ALDO1 | LDO | 0.5-3.5V | 100mV | 300mA | Peripheral power |
| ALDO2 | LDO | 0.5-3.5V | 100mV | 300mA | Peripheral power |
| ALDO3 | LDO | 0.5-3.5V | 100mV | 300mA | Peripheral power |
| ALDO4 | LDO | 0.5-3.5V | 100mV | 300mA | Peripheral power |
| BLDO1 | LDO | 0.5-3.5V | 100mV | 300mA | AMOLED/display related |
| BLDO2 | LDO | 0.5-3.5V | 100mV | 300mA | Microphone/audio |
| DLDO1 | LDO | 0.5-3.3V | 100mV | 300mA | Optional |
| DLDO2 | LDO | 0.5-3.3V (via DCDC) | | 300mA | Optional |
| CPUSLDO | LDO | 0.5-1.4V | 50mV | 30mA | CPU sleep LDO |

### Battery Charger Features
- Charge current: Configurable (25mA - 1000mA)
- Pre-charge current: 25mA, 50mA, 75mA, 100mA
- Termination current: 25mA
- Charge target voltage: 4.0V, 4.1V, 4.2V, 4.35V, 4.4V
- Battery detection: Automatic
- Over-charge/over-discharge protection

### Fuel Gauge
- Battery percentage estimation (0-100%)
- VBAT voltage monitoring via ADC
- VBUS voltage monitoring
- VSYS voltage monitoring
- Temperature sensor (TS pin) for battery thermistor

### Power Key (PWR Button)
- Configurable short press / long press actions
- Short press: Typically toggle power state
- Long press (configurable 1-4 seconds): Force power off
- IRQ generation on press events

### Key IRQ Events
- Battery inserted/removed
- VBUS (USB) connected/disconnected
- Charge started/completed
- Power key short press / long press
- Over-temperature warning
- Low battery warning

### Software Library
- **Arduino:** `XPowersLib` by Lewis He (lewisxhe)
  - `#include <XPowersLib.h>`
  - Key API: `pmu.begin()`, `pmu.getBattVoltage()`, `pmu.getBatteryPercent()`, `pmu.isCharging()`, `pmu.setChargeCurrent()`, `pmu.enableALDO1()`, etc.
- **ESP-IDF:** XPowersLib also supports ESP-IDF via component
  - Also: `protoconcept/axp2101` ESP-IDF component on GitHub
- **ESPHome:** `esphome-axp2101` component by Stefan Thoss

---

## 4. QMI8658 IMU (0x6B)

### Overview
- I2C Address: **0x6B** (`QMI8658_L_SLAVE_ADDRESS`, SA0 pulled high on this board)
- Manufacturer: QST Corporation
- Type: 6-axis IMU (3-axis accelerometer + 3-axis gyroscope)

### Accelerometer Specifications
| Parameter | Value |
|-----------|-------|
| Full-scale range | ±2g, ±4g, ±8g, ±16g (configurable) |
| Default on this board | ±4g (from example code) |
| Resolution | 16-bit |
| Noise density | 200 µg/√Hz |
| ODR (Output Data Rate) | 1000 Hz (from example code) |
| ODR range | 1 Hz to 8000 Hz |
| Zero-g offset | ±30 mg |

### Gyroscope Specifications
| Parameter | Value |
|-----------|-------|
| Full-scale range | ±16, ±32, ±64, ±128, ±256, ±512, ±1024, ±2048 °/s |
| Resolution | 16-bit |
| Noise density | 15 mdps/√Hz |
| ODR range | 1 Hz to 8000 Hz |
| Zero-rate offset | ±3 °/s |

### Features
- On-chip sensor fusion (Attitude Engine)
- FIFO buffer (up to 128 samples)
- Programmable digital low-pass filters
- Motion/no-motion detection
- Tap/double-tap detection (via accelerometer)
- Temperature sensor (resolution 1/256 °C)
- Wake-on-motion for deep sleep wakeup
- Step counter / pedometer (built-in)

### Use Cases on This Board
1. **Auto-rotate display** - Use accelerometer to detect orientation
2. **Gesture control** - Tilt/shake detection
3. **Pedometer** - Step counting for watch applications
4. **Sleep tracking** - Motion detection for activity monitoring
5. **Wrist-raise detection** - Wake screen on wrist raise

### Software Library
- **Arduino:** `SensorLib` v0.3.3 by Lewis He
  - Class: `SensorQMI8658`
  - Key API: `qmi.begin()`, `qmi.configAccelerometer()`, `qmi.configGyroscope()`, `qmi.getDataReady()`, `qmi.getAccelerometer()`, `qmi.getGyroscope()`
- **ESP-IDF:** `esp-cpp/espp` includes QMI8658 driver, or use raw I2C

### Initialization Pattern (Arduino)
```cpp
#include "SensorQMI8658.hpp"
SensorQMI8658 qmi;

qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
qmi.configAccelerometer(
    SensorQMI8658::ACC_RANGE_4G,
    SensorQMI8658::ACC_ODR_1000Hz,
    SensorQMI8658::LPF_MODE_0);
qmi.enableAccelerometer();

// Optional gyroscope:
qmi.configGyroscope(
    SensorQMI8658::GYR_RANGE_512DPS,
    SensorQMI8658::GYR_ODR_896_8Hz,
    SensorQMI8658::LPF_MODE_3);
qmi.enableGyroscope();
```

---

## 5. PCF85063 RTC (0x51)

### Overview
- I2C Address: **0x51** (fixed, no address pins)
- Manufacturer: NXP Semiconductors
- Variant: PCF85063A
- Current: ~0.22 µA in backup mode

### Features
- Seconds, minutes, hours, day, weekday, month, year
- Alarm: Single alarm with minute, hour, day, weekday match
- Timer: Countdown timer with interrupt
- Clock output: 32.768 kHz, 16.384 kHz, 8.192 kHz, 4.096 kHz, 2.048 kHz, 1.024 kHz, 1 Hz
- Accuracy: ±2 ppm (typ) at 25°C
- INT output: Open-drain, active low (alarm or timer interrupt)
- Backup battery: Pads on board for backup battery to keep time during main power loss

### Register Map
| Address | Register | Description |
|---------|----------|-------------|
| 0x00 | Control_1 | EXT_TEST, STOP, SR, CIE, 12_24, MI, SI |
| 0x01 | Control_2 | AIE, TIE, AF, TF, COF[2:0] |
| 0x02 | Offset | Frequency offset calibration |
| 0x03 | RAM_byte | 1 byte general-purpose RAM |
| 0x04 | Seconds | BCD seconds (0-59) + OS flag |
| 0x05 | Minutes | BCD minutes (0-59) |
| 0x06 | Hours | BCD hours (0-23 or 1-12 + AM/PM) |
| 0x07 | Days | BCD day of month (1-31) |
| 0x08 | Weekdays | Weekday (0-6) |
| 0x09 | Months | BCD month (1-12) |
| 0x0A | Years | BCD year (0-99) |
| 0x0B | Second_alarm | Alarm: seconds match |
| 0x0C | Minute_alarm | Alarm: minutes match |
| 0x0D | Hour_alarm | Alarm: hours match |
| 0x0E | Day_alarm | Alarm: day match |
| 0x0F | Weekday_alarm | Alarm: weekday match |
| 0x10 | Timer_value | Countdown timer value |
| 0x11 | Timer_mode | Timer clock frequency + enable |

### Deep Sleep Wakeup via RTC
The PCF85063 INT pin can be connected to an ESP32-C6 RTC GPIO to wake from deep sleep on alarm. Check schematic for INT pin routing (may go through IO expander or directly to a GPIO).

### Software Library
- **Arduino:** `SensorLib` includes `SensorPCF85063` class
  - Also: dedicated `PCF85063A` library by ambiguous
  - Also: RTClib by Adafruit (generic RTC support)
- **ESP-IDF:** `esp-cpp/espp` includes PCF85063 driver
  - Also: raw I2C register access (simple BCD read/write)

---

## 6. ES8311 Audio Codec (0x18)

### Overview
- I2C Address: **0x18** (default; 0x19 if ASEL pin high)
- Manufacturer: Everest Semiconductor
- Type: Low-power mono audio codec
- Control: I2C (register config)
- Audio data: I2S (digital audio stream)
- I2C speed: up to 400 kHz

### Audio Specifications

| Parameter | ADC (Mic Input) | DAC (Speaker Output) |
|-----------|-----------------|---------------------|
| Resolution | 24-bit | 24-bit |
| SNR | 97 dB (typ) | 99 dB (typ) |
| THD+N | -84 dB | -86 dB |
| Sample rates | 8/11.025/16/22.05/32/44.1/48/96 kHz | Same |
| Dynamic range | 97 dB | 99 dB |

### I2S Interface (from pin_config.h)
```
GPIO 19 → MCLK  (Master Clock, typically 256*Fs)
GPIO 20 → BCLK  (Bit Clock)
GPIO 22 → WS    (Word Select / LRCK)
GPIO 21 → DIN   (ESP32 receives mic data)
GPIO 23 → DOUT  (ESP32 sends speaker data)
```

### Board Audio Hardware
- **Microphone:** Dual digital microphone array (onboard)
- **Speaker:** Onboard speaker
- **Power amplifier:** Likely controlled via a GPIO or IO expander pin (PA enable)

### Key Registers
| Address | Register | Description |
|---------|----------|-------------|
| 0x00 | Reset | Software reset, state machine control |
| 0x01 | CLK Manager 1 | MCLK source and divider |
| 0x02 | CLK Manager 2 | ADC/DAC clock divider |
| 0x06 | CLK Manager 6 | BCLK divider |
| 0x09 | SDP In | ADC data format (I2S/LJ/RJ, bit width) |
| 0x0A | SDP Out | DAC data format |
| 0x0D | System | Mic/speaker enable |
| 0x14 | ADC Control | Mic PGA gain |
| 0x17 | ADC Volume | Digital ADC volume (0-255) |
| 0x32 | DAC Volume | Digital DAC volume (0-255) |

### XiaoZhi AI Integration
This board is designed for the [XiaoZhi AI](https://github.com/78/xiaozhi-esp32) voice assistant framework:
- Firmware v2.2.4+ supports this board
- Offline speech recognition using on-device models
- Online LLM integration (DeepSeek, Doubao, etc.)
- Wake word detection → record via mic → process → respond via speaker
- The dual microphone array enables noise cancellation and beam-forming

### ESP-IDF Component
- `espressif/es8311` v1.0.0 on Component Registry
- Also via ESP-ADF (Audio Development Framework)
- Built-in example: `esp-idf/examples/peripherals/i2s/i2s_codec/i2s_es8311`

---

## 7. Initialization Order (Critical)

The board requires a specific initialization sequence due to power dependencies:

```
1. I2C bus init (GPIO 7 SCL, GPIO 8 SDA, 200 kHz)
   │
2. AXP2101 PMIC init (0x34)
   ├── Configure power rails (DCDC, ALDO, BLDO)
   ├── Set charge parameters
   └── Enable required outputs
   │
3. TCA9554 IO Expander init (0x20)
   ├── Set P4 = OUTPUT, P5 = OUTPUT
   ├── P4 = LOW, P5 = LOW (reset)
   ├── Delay 200ms
   ├── P4 = HIGH (display power ON)
   └── P5 = HIGH (touch power ON)
   │
4. Display init (SH8601 via QSPI)
   ├── QSPI bus init (GPIO 0-5)
   ├── Sleep Out command + 120ms delay
   ├── Set resolution (368x448)
   └── Display On + brightness
   │
5. Touch init (FT3168 via I2C 0x38)
   │
6. Optional peripheral init (any order):
   ├── QMI8658 IMU (0x6B)
   ├── PCF85063 RTC (0x51)
   ├── ES8311 Audio (0x18) + I2S bus
   └── SD Card (SDMMC GPIO 10,11,18)
```

---

## 8. Power Architecture Diagram

```
USB-C (5V)
    │
    ▼
┌──────────────┐
│  AXP2101     │◄──── Battery (3.7V LiPo, MX1.25)
│  PMIC (0x34) │
│              │
│  DCDC1 ──────┼──► ESP32-C6 core (3.3V, 2A max)
│  DCDC3 ──────┼──► Peripheral 3.3V
│  ALDO1-4 ────┼──► Various 1.8V/2.8V/3.3V peripherals
│  BLDO1 ──────┼──► AMOLED display power
│  BLDO2 ──────┼──► Microphone power
│              │
│  Charger ────┼──► Battery charge (configurable mA)
│  Fuel Gauge ─┼──► Battery % estimation
│  PWR Key ────┼──► Power ON/OFF button
└──────────────┘
        │
        ▼
┌──────────────┐
│  TCA9554     │
│  IO Exp(0x20)│
│              │
│  P4 ─────────┼──► AMOLED power enable (HIGH=ON)
│  P5 ─────────┼──► Touch power enable (HIGH=ON)
│  P0-P3,P6-P7┼──► Unknown / available
└──────────────┘
```

---

## 9. Deep Sleep Considerations

For minimal power consumption:

1. **AXP2101:** Turn off unnecessary power rails (ALDO, BLDO for display/audio)
2. **SH8601:** Enter deep standby mode (register 0x4F) before cutting power
3. **FT3168:** Set to hibernate mode (register 0xA5 = 0x03)
4. **QMI8658:** Can wake ESP32 via motion interrupt
5. **PCF85063:** Can wake ESP32 via alarm interrupt
6. **ES8311:** Power down codec
7. **ESP32-C6:** Enter deep sleep with RTC GPIO wakeup source

Reported deep sleep current:
- **52 µA at 3.7V** (battery)
- **392 µA at 3.3V** (USB without battery, AXP2101 less efficient)
