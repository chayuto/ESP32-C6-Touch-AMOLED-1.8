# PCF85063A RTC & ES8311 Audio Codec - Peripheral Deep Dive

> Deep research on the two secondary peripherals of the Waveshare ESP32-C6-Touch-AMOLED-1.8 board.
> Date: 2026-04-03
> Sources: NXP PCF85063A datasheet Rev.7, Everest ES8311 datasheet Rev.10.0, ES8311 User Guide Rev.1.11,
> Waveshare official docs, ESP-IDF component registry, Arduino library repositories.

---

## Table of Contents

1. [Part 1: PCF85063A Real-Time Clock](#part-1-pcf85063a-real-time-clock)
2. [Part 2: ES8311 Audio Codec](#part-2-es8311-audio-codec)
3. [XiaoZhi AI Voice Assistant Integration](#xiaozhi-ai-voice-assistant-integration)
4. [Development Strategy for This Board](#development-strategy-for-this-board)

---

# Part 1: PCF85063A Real-Time Clock

## 1.1 Chip Identification: PCF85063A (Not PCF85063)

The board uses the **PCF85063A** variant specifically. NXP makes several variants in this family:

| Variant | Features | I_DD (typ) | Interface | Battery Backup |
|---------|----------|-----------|-----------|----------------|
| **PCF85063TP** | Basic functions only, NO alarm | 220 nA | I2C | No |
| **PCF85063A** | **Alarm + Timer + Tiny package** | **220 nA** | **I2C** | **No (external)** |
| PCF85063B | Same as A but SPI interface | 220 nA | SPI | No |
| PCF85263A | Alarm + Timer + Battery backup built-in | 230 nA | I2C | Yes |

Key distinction: The PCF85063A has alarm and timer functions (which PCF85063TP lacks) but does NOT have built-in battery backup switching (which the PCF85263A has). On this Waveshare board, battery backup is handled externally via dedicated pads on the PCB.

## 1.2 I2C Address

**Fixed I2C slave address: 0x51** (7-bit address)

The full 8-bit address byte on the wire is:
- Write: `0xA2` (1010 0010)
- Read: `0xA3` (1010 0011)

The slave address is `1010001` in binary (bits 7-1), which equals `0x51` in 7-bit notation. This address is hardwired and cannot be changed. It shares the I2C bus with QMI8658 (0x6B), AXP2101 (0x34), ES8311 (0x18), and FT3168 touch controller (0x38).

## 1.3 Register Map

The PCF85063A has 18 registers (0x00 to 0x11) with auto-incrementing address pointer that wraps from 0x11 back to 0x00.

### Control and Status Registers

| Address | Name | Key Bits |
|---------|------|----------|
| 0x00 | Control_1 | EXT_TEST[7], STOP[5], SR[4], CIE[2], 12_24[1], CAP_SEL[0] |
| 0x01 | Control_2 | AIE[7], AF[6], MI[5], HMI[4], TF[3], COF[2:0] |
| 0x02 | Offset | MODE[7], OFFSET[6:0] |
| 0x03 | RAM_byte | 8-bit free RAM for any user purpose |

### Time and Date Registers (BCD encoded)

| Address | Name | Range | Notes |
|---------|------|-------|-------|
| 0x04 | Seconds | 0-59 | Bit 7 = OS (oscillator stop flag) |
| 0x05 | Minutes | 0-59 | |
| 0x06 | Hours | 0-23 or 1-12 | Bit 5 = AMPM in 12h mode |
| 0x07 | Days | 1-31 | |
| 0x08 | Weekdays | 0-6 | Sunday=0 (reassignable) |
| 0x09 | Months | 1-12 | |
| 0x0A | Years | 0-99 | Leap year auto-calculated (div by 4) |

### Alarm Registers

| Address | Name | Enable Bit | Match Field |
|---------|------|-----------|-------------|
| 0x0B | Second_alarm | AEN_S[7] (0=enabled) | Second (0-59) |
| 0x0C | Minute_alarm | AEN_M[7] (0=enabled) | Minute (0-59) |
| 0x0D | Hour_alarm | AEN_H[7] (0=enabled) | Hour |
| 0x0E | Day_alarm | AEN_D[7] (0=enabled) | Day (1-31) |
| 0x0F | Weekday_alarm | AEN_W[7] (0=enabled) | Weekday (0-6) |

### Timer Registers

| Address | Name | Description |
|---------|------|-------------|
| 0x10 | Timer_value | 8-bit countdown value (1-255) |
| 0x11 | Timer_mode | TCF[4:3], TE[2], TIE[1], TI_TP[0] |

## 1.4 Alarm Capabilities

### Single Alarm with Multi-Field Matching

The PCF85063A provides a single alarm with five independently enableable match fields:
- Second, Minute, Hour, Day-of-month, Day-of-week

Each field has an enable bit (AEN_x). Setting AEN_x = 0 **enables** that field for comparison; AEN_x = 1 **disables** (ignores) it. The alarm fires when ALL enabled fields match simultaneously.

**Alarm use cases:**
- "Every day at 07:30:00" -- enable minute + hour, disable second/day/weekday
- "Every Monday at 09:00" -- enable minute + hour + weekday
- "Once on the 15th at noon" -- enable hour + day
- "Every minute at :30 seconds" -- enable second only

**Important behaviors:**
- When alarm fires: AF flag (Control_2 bit 6) is set to 1
- AF stays set until cleared by writing 0 to it
- If AIE (Control_2 bit 7) is enabled, the INT pin goes LOW when AF is set
- After clearing AF, alarm will fire again on the next match

### Countdown Timer

Separate from the alarm, the 8-bit countdown timer provides periodic or one-shot interrupts:

| TCF[1:0] | Source Clock | Min Period (T=1) | Max Period (T=255) |
|----------|-------------|-------------------|---------------------|
| 00 | 4.096 kHz | 244 us | 62.256 ms |
| 01 | 64 Hz | 15.625 ms | 3.984 s |
| 10 | 1 Hz | 1 s | 255 s (~4.25 min) |
| 11 | 1/60 Hz | 60 s | 4 hours 15 min |

Timer modes via TI_TP bit:
- **TI_TP = 1 (pulsed):** INT generates a pulse on every countdown; timer auto-reloads and repeats
- **TI_TP = 0 (level):** INT follows TF flag; timer stops after first countdown unless TF is cleared

### Minute and Half-Minute Interrupts

Additional periodic interrupt sources (MI and HMI bits in Control_2):
- MI = 1: interrupt every minute
- HMI = 1: interrupt every 30 seconds
- Both = 1: interrupt every 30 seconds

## 1.5 Interrupt Output (INT Pin)

The INT pin is an **open-drain** output, active LOW. It requires an external pull-up resistor. On this board, the INT pin is connected to an ESP32-C6 GPIO (specific pin assignment should be verified from the schematic, but the board does connect it).

**Interrupt sources (active LOW on INT):**
1. **Alarm interrupt** -- when AIE=1 and AF=1, INT stays LOW until AF is cleared
2. **Timer interrupt** -- when TIE=1 and timer expires (behavior depends on TI_TP setting)
3. **Minute/half-minute interrupt** -- when MI or HMI enabled, produces 1/64 second pulses
4. **Correction interrupt** -- when CIE=1, pulses during offset correction

The INT pin can be used to wake the ESP32-C6 from deep sleep via GPIO wakeup.

## 1.6 Backup Battery Support

The PCF85063A itself does NOT have a built-in battery backup switch (unlike the PCF85263A). However:

- The chip operates from **0.9V to 5.5V** supply range
- At 0.9V minimum, it will keep running from a very low voltage source
- The Waveshare board has **dedicated backup battery pads** on the PCB

**How backup works on this board:**
The backup battery pads connect through a low-Vf diode to the PCF85063A VDD pin. When main power is removed (e.g., swapping the main lithium battery), the backup battery maintains RTC power. The datasheet's application circuit (Fig. 31) shows a super capacitor (1F) with a current-limiting resistor and low-Vf diode as a reference design for backup. With the RTC in minimum power configuration (timer off, CLKOUT off), the chip can run for weeks on a small backup source.

**For the Waveshare board:** solder a small coin cell or rechargeable cell to the backup battery pads. This is not required for normal operation but is essential if you need the RTC to survive main battery removal.

## 1.7 Clock Output Frequencies

The CLKOUT pin provides a programmable square wave, controlled by COF[2:0] in Control_2:

| COF[2:0] | Frequency | Duty Cycle | Affected by STOP? |
|----------|-----------|------------|-------------------|
| 000 (default) | 32,768 Hz | ~60:40 | No |
| 001 | 16,384 Hz | 50:50 | No |
| 010 | 8,192 Hz | 50:50 | No |
| 011 | 4,096 Hz | 50:50 | CLKOUT = LOW |
| 100 | 2,048 Hz | 50:50 | CLKOUT = LOW |
| 101 | 1,024 Hz | 50:50 | CLKOUT = LOW |
| 110 | 1 Hz | 50:50 | CLKOUT = LOW |
| 111 | CLKOUT disabled (LOW) | - | - |

CLKOUT is a push-pull output. To save power, set COF = 111 to disable it.

## 1.8 Accuracy

**Oscillator frequency variation:** typ 0.075 ppm for 200 mV supply change at 25C.

**Without calibration:** Accuracy depends primarily on the 32.768 kHz crystal quality and temperature. A typical quartz crystal drifts ~20 ppm over the -40 to +85C range, which is roughly 1.7 seconds per day.

**With offset calibration:**
- The Offset register (0x02) allows fine-tuning in two modes:
  - **MODE = 0 (normal):** correction every 2 hours; 4.34 ppm per LSB; range +/-277 ppm
  - **MODE = 1 (fast):** correction every 4 minutes; 4.069 ppm per LSB; range +/-260 ppm
- With calibration, accuracy of **+/- 2 ppm** can be achieved (about 0.17 seconds per day, or ~1 minute per year)

**Calibration procedure:**
1. Measure the actual CLKOUT frequency (set to 32768 Hz)
2. Calculate deviation in ppm: `E_ppm = 1,000,000 * (1/32768 - 1/f_measured) / (1/f_measured)`
3. Compute offset value: `Offset = E_ppm / 4.34` (MODE=0) or `E_ppm / 4.069` (MODE=1)
4. Write the 7-bit two's complement value to the Offset register

## 1.9 Current Consumption

### Standby / Timekeeping Current

| Condition | VDD | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Interface inactive, CLKOUT disabled, CL=7pF | 3.3V | 220 | 450 | nA |
| Interface inactive, CLKOUT disabled, CL=7pF | 5.5V | - | - | nA |
| Interface inactive, CLKOUT disabled, CL=12.5pF | 3.3V | 250 | 500 | nA |

At 3.3V, 25C, CLKOUT disabled: **typical 220 nA** (0.22 uA).

### Active / I2C Communication Current

| Condition | VDD | Typ | Unit |
|-----------|-----|-----|------|
| Interface active, fSCL = 400 kHz | 3.3V | ~8 | uA |
| Interface active, fSCL = 400 kHz | 5.0V | ~16 | uA |

### CLKOUT Enabled (adds to base current)

With CLKOUT at 32768 Hz, a 22pF load adds ~4 uA at 3.3V. The 1 Hz output adds negligible current.

### Temperature Dependency

Current increases with temperature. At 85C, the standby current approximately doubles compared to 25C (typ ~450 nA at 3.3V).

**Key takeaway for battery life:** At 220 nA typical, a CR2032 coin cell (225 mAh) could power the RTC for over 100 years in theory. In practice, battery self-discharge limits this, but years of backup is realistic.

## 1.10 Arduino Library Options

### 1. SolderedElectronics PCF85063A Library (Recommended)
- **Repository:** https://github.com/SolderedElectronics/PCF85063A-Arduino-Library
- **License:** GPL 3.0
- **Architecture:** All (works on ESP32)
- **Features:** Set/get time, alarm, timer, CLKOUT control
- **Install:** Arduino Library Manager search "PCF85063A"

### 2. RTC_NXP_Arduino
- **Repository:** Available via Arduino Library Manager
- **Supports:** PCF85063A plus other NXP RTCs (PCF8523, PCF85263A, etc.)
- **Benefits:** Unified API across NXP RTC family

### 3. Waveshare's Own Library ("Mylibrary")
- Included in the demo package at `examples/Arduino-v3.3.5/libraries/`
- Custom wrapper around direct I2C register access
- Used in examples `05_GFX_PCF85063_simpleTime` and `10_LVGL_PCF85063_simpleTime`

### 4. Direct I2C Register Access
For maximum control, use Wire library directly:
```cpp
#include <Wire.h>
#define PCF85063A_ADDR 0x51

// Read time (registers 0x04-0x0A)
Wire.beginTransmission(PCF85063A_ADDR);
Wire.write(0x04);  // Start at Seconds register
Wire.endTransmission(false);
Wire.requestFrom(PCF85063A_ADDR, 7);
uint8_t seconds = bcd2dec(Wire.read() & 0x7F);  // Mask OS bit
uint8_t minutes = bcd2dec(Wire.read() & 0x7F);
uint8_t hours   = bcd2dec(Wire.read() & 0x3F);  // 24h mode
// ... days, weekdays, months, years
```

## 1.11 ESP-IDF Driver Options

### Waveshare Official ESP-IDF Example
- Located at: `examples/ESP-IDF-v5.5.1/02_PCF85063/`
- Direct register-level driver using ESP-IDF I2C master API
- Demonstrates time setting and reading

### ESP-IDF I2C Master API (Direct)
The ESP-IDF provides `i2c_master` APIs that work well for direct register access:
```c
#include "driver/i2c_master.h"

// Write time to registers 0x04-0x0A
uint8_t time_data[8] = {0x04, sec, min, hour, day, weekday, month, year};
i2c_master_transmit(dev_handle, time_data, sizeof(time_data), -1);

// Read time from registers 0x04-0x0A
uint8_t reg = 0x04;
uint8_t buf[7];
i2c_master_transmit_receive(dev_handle, &reg, 1, buf, 7, -1);
```

### Third-Party ESP-IDF Components
- `costa-victor/ESP32-PCF85063A` on GitHub -- basic ESP-IDF driver

## 1.12 Time Setting and Reading Patterns

### Setting Time (Recommended Procedure)

1. **Stop the clock:** Write to Control_1 (0x00) with STOP bit = 1
2. **Write all time registers in one I2C transaction:** Start at 0x04, write seconds through years (7 bytes). This ensures atomic update since time counters are frozen during I2C access.
3. **Clear the OS flag:** Write seconds with OS bit = 0
4. **Restart the clock:** Write to Control_1 with STOP bit = 0

The first time increment after clearing STOP occurs between 0.508 and 0.508 seconds due to prescaler uncertainty.

### Reading Time (Recommended Procedure)

Read all 7 bytes (0x04-0x0A) in a single I2C burst read. This is critical because:
- Time counters are frozen during the I2C access, preventing inconsistent reads
- The entire I2C transaction must complete within 1 second
- Check the OS flag (bit 7 of Seconds register) to verify clock integrity

### BCD Conversion

All time values are stored in BCD format:
```c
uint8_t bcd2dec(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }
uint8_t dec2bcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }
```

### 12-Hour vs 24-Hour Mode

Controlled by bit 1 (12_24) in Control_1 register:
- 0 = 24-hour mode (default)
- 1 = 12-hour mode (bit 5 of Hours register becomes AM/PM indicator)

## 1.13 Wakeup from Deep Sleep via RTC Alarm

This is one of the most useful features for a battery-powered wearable. The pattern:

### Setup (Before Entering Deep Sleep)

1. **Configure the alarm:**
   ```c
   // Set alarm for 07:30:00 daily
   uint8_t alarm_data[] = {
       0x0B,           // Start at Second_alarm register
       0x00,           // Second alarm: 00, AEN_S=0 (enabled)
       0x30,           // Minute alarm: 30, AEN_M=0 (enabled)
       0x07,           // Hour alarm: 07, AEN_H=0 (enabled)
       0x80,           // Day alarm: disabled (AEN_D=1)
       0x80            // Weekday alarm: disabled (AEN_W=1)
   };
   i2c_master_transmit(dev_handle, alarm_data, sizeof(alarm_data), -1);
   ```

2. **Enable alarm interrupt:**
   ```c
   // Control_2: AIE=1 (bit 7), clear AF (bit 6), keep other settings
   uint8_t ctrl2_data[] = {0x01, 0x80};  // AIE=1, AF=0
   i2c_master_transmit(dev_handle, ctrl2_data, sizeof(ctrl2_data), -1);
   ```

3. **Configure ESP32-C6 GPIO wakeup:**
   ```c
   gpio_wakeup_enable(RTC_INT_GPIO, GPIO_INTR_LOW_LEVEL);
   esp_sleep_enable_gpio_wakeup();
   esp_deep_sleep_start();
   ```

### After Wakeup

1. Check if wakeup was from GPIO (RTC alarm)
2. Read and clear the AF flag:
   ```c
   uint8_t reg = 0x01;
   uint8_t ctrl2;
   i2c_master_transmit_receive(dev_handle, &reg, 1, &ctrl2, 1, -1);
   ctrl2 &= ~0x40;  // Clear AF (bit 6), keep AIE set
   uint8_t clear_af[] = {0x01, ctrl2};
   i2c_master_transmit(dev_handle, clear_af, sizeof(clear_af), -1);
   ```

### Timer-Based Wakeup (Alternative)

For periodic wakeup (e.g., every 5 minutes):
```c
// Timer_value = 5, Timer_mode: 1Hz source, TE=1, TIE=1, TI_TP=1 (pulsed)
uint8_t timer_data[] = {0x10, 0x05};  // 5 second countdown (use 1/60 Hz for minutes)
i2c_master_transmit(dev_handle, timer_data, sizeof(timer_data), -1);

// For 5-minute wakeup: TCF=11 (1/60 Hz source), T=5, TE=1, TIE=1, TI_TP=1
uint8_t mode_data[] = {0x11, 0x1F};  // 00 011 1 1 1 = TCF=11, TE=1, TIE=1, TI_TP=1
i2c_master_transmit(dev_handle, mode_data, sizeof(mode_data), -1);
```

---

# Part 2: ES8311 Audio Codec

## 2.1 Chip Overview

The ES8311 is a **low-power mono audio codec** by Everest Semiconductor (Shanghai). It contains:
- One mono ADC (microphone input path)
- One mono DAC (speaker/headphone output path)
- I2S/PCM serial audio interface
- I2C control interface
- Internal clock management with PLL

It is specifically designed for voice applications: surveillance, smart speakers, intercom, IP cameras.

## 2.2 I2C Address

The I2C address is determined by the **CE (chip enable) pin**:

| CE Pin State | 7-bit I2C Address | Binary |
|-------------|-------------------|--------|
| Pulled LOW (10k to GND) | **0x18** | 0011 000 |
| Pulled HIGH (10k to PVDD) | **0x19** | 0011 001 |

On the Waveshare ESP32-C6-Touch-AMOLED-1.8 board, the CE pin is pulled LOW, so the **I2C address is 0x18**.

The I2C clock rate supports up to 400 kHz (fast mode).

## 2.3 I2S Interface Configuration

### Supported Formats

| Format | SDP_IN_FMT / SDP_OUT_FMT value |
|--------|-------------------------------|
| I2S (Philips standard) | 0 (default) |
| Left Justified | 1 |
| DSP/PCM Mode A | 3 |
| DSP/PCM Mode B | 3 (with LRP bit) |

### Word Lengths

Configurable via SDP_IN_WL and SDP_OUT_WL (registers 0x09, 0x0A):

| Setting | Word Length |
|---------|-----------|
| 0 (default) | 24-bit |
| 1 | 20-bit |
| 2 | 18-bit |
| 3 | 16-bit |
| 4 | 32-bit |

### Master vs Slave Mode

Controlled by MSC bit (Register 0x00, bit 6):
- **MSC = 0 (default):** Slave mode -- LRCK and SCLK are inputs from ESP32-C6
- **MSC = 1:** Master mode -- ES8311 generates LRCK and SCLK

**For this board, slave mode is typical.** The ESP32-C6 I2S peripheral acts as master, providing MCLK, SCLK (BCLK), and LRCK (WS) to the ES8311.

### Pin Connections on This Board

| ES8311 Pin | Function | ESP32-C6 Connection |
|-----------|----------|---------------------|
| MCLK | Master clock input | I2S MCLK output from ESP32-C6 |
| SCLK/DMIC_SCL | Bit clock | I2S BCLK |
| LRCK | Frame sync (word select) | I2S WS |
| ASDOUT | ADC data output (mic) | I2S DIN (data in to ESP32) |
| DSDIN | DAC data input (speaker) | I2S DOUT (data out from ESP32) |
| CCLK | I2C clock | Shared I2C SCL bus |
| CDATA | I2C data | Shared I2C SDA bus |

### Clock Configuration

The ES8311 supports many MCLK frequencies:
- Standard audio clocks: 12.288 MHz, 24.576 MHz (for 48kHz family)
- 11.2896 MHz, 22.5792 MHz (for 44.1kHz family)
- USB clocks: 12 MHz, 24 MHz
- Non-standard: 16 MHz, 25 MHz, 26 MHz

Internal clock dividers and multipliers allow flexible configuration:
- `DIV_PRE` (Reg 0x02, bits 7:5): Pre-divide MCLK by 1-8
- `MULT_PRE` (Reg 0x02, bits 4:3): Multiply by 1, 2, 4, or 8
- `DIV_CLKADC` and `DIV_CLKDAC` (Reg 0x05): Independent ADC/DAC clock dividers

**Common configuration table (from User Guide):**

| MCLK (MHz) | LRCK (kHz) | Reg 0x02 | Reg 0x05 | Reg 0x16 | Reg 0x03 | Reg 0x04 |
|-----------|-----------|----------|----------|----------|----------|----------|
| 12.288 | 48 | 0x00 | 0x00 | 0x04 | 0x10 | 0x10 |
| 12.288 | 32 | 0x48 | 0x00 | 0x04 | 0x10 | 0x10 |
| 12.288 | 8 | 0x00 | 0x00 | 0x04 | 0x19 | 0x19 |

## 2.4 ADC Specifications (Microphone Input)

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Signal-to-Noise Ratio (A-weighted) | 95 | 100 | 102 | dB |
| THD+N | -95 | -93 | -85 | dB |
| Gain Error | | | +/-5 | % |
| Full Scale Input Level | | +/-AVDD/3.3 | | Vrms |
| Input Impedance | | 6 | | kohm |
| Sampling Frequency (single speed) | 8 | | 48 | kHz |
| Sampling Frequency (double speed) | 64 | | 96 | kHz |
| Resolution | | 24 | | bits |

### Filter Characteristics (Single Speed Mode)

| Parameter | Value |
|-----------|-------|
| Passband | 0 to 0.4535 * Fs |
| Stopband | 0.5465 * Fs |
| Passband Ripple | +/-0.05 dB |
| Stopband Attenuation | 70 dB |

## 2.5 DAC Specifications (Speaker Output)

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| Signal-to-Noise Ratio (A-weighted) | 105 | 110 | 115 | dB |
| THD+N | -85 | -80 | -75 | dB |
| Gain Error | | | +/-5 | % |
| Full Scale Output Level | | +/-0.9*AVDD/3.3 | | Vrms |
| Sampling Frequency (single speed) | 8 | | 48 | kHz |
| Resolution | | 24 | | bits |

### Filter Characteristics (Single Speed Mode)

| Parameter | Value |
|-----------|-------|
| Passband | 0 to 0.4535 * Fs |
| Stopband | 0.5465 * Fs |
| Passband Ripple | +/-0.05 dB |
| Stopband Attenuation | 53 dB |

### Headphone/Line Output

The OUTP/OUTN differential output can drive:
- **16 ohm** or **32 ohm** headphones directly
- External power amplifier for speaker drive

On this board, OUTP/OUTN likely feed an external class-D amplifier which drives the onboard speaker.

## 2.6 Analog Gain Stages

### Input Path (ADC / Microphone)

**1. PGA (Programmable Gain Amplifier)**
- Register: 0x14, bits 3:0 (PGAGAIN)
- Range: 0 dB to 30 dB in 3 dB steps

| PGAGAIN | Gain |
|---------|------|
| 0 | 0 dB |
| 1 | 3 dB |
| 2 | 6 dB |
| 3 | 9 dB |
| 4 | 12 dB |
| 5 | 15 dB |
| 6 | 18 dB |
| 7 | 21 dB |
| 8 | 24 dB |
| 9 | 27 dB |
| 10 | 30 dB |

**2. ADC Digital Volume**
- Register: 0x17 (ADC_VOLUME)
- Range: -95.5 dB to +32 dB in 0.5 dB steps
- 0xBF = 0 dB, 0x00 = -95.5 dB, 0xFF = +32 dB

**3. ADC Gain Scale (compensates for low OSR)**
- Register: 0x16, bits 2:0 (ADC_SCALE)
- Range: 0 to 42 dB in 6 dB steps (default: 4 = 24 dB)

**4. ALC (Automatic Level Control)**
- Enable: Register 0x18, bit 7 (ALC_EN)
- Max level: Register 0x19, bits 7:4 (ALC_MAXLEVEL): -30.1 dB to -6.0 dB
- Min level: Register 0x19, bits 3:0 (ALC_MINLEVEL): -30.1 dB to -6.0 dB
- Auto-mute: Register 0x18, bit 6 (ADC_AUTOMUTE_EN)
- Noise gate: Register 0x1A, bits 3:0 (ADC_AUTOMUTE_NG): -96 dB to -30 dB

### Output Path (DAC / Speaker)

**1. DAC Digital Volume**
- Register: 0x32 (DAC_VOLUME)
- Range: -95.5 dB to +32 dB in 0.5 dB steps
- 0xBF = 0 dB

**2. DRC (Dynamic Range Compression)**
- Enable: Register 0x34, bit 7 (DRC_EN)
- Max level: Register 0x35, bits 7:4 (DRC_MAXLEVEL): -30.1 dB to -6.0 dB
- Min level: Register 0x35, bits 3:0 (DRC_MINLEVEL): -30.1 dB to -6.0 dB

**3. HP Driver Output Mode**
- Register 0x13, bit 4 (HPSW): 0 = line out (default), 1 = HP drive mode

## 2.7 Microphone Bias Voltage

**The ES8311 does NOT have a dedicated MICBIAS pin.**

Instead, the AVDD power supply pin is used as microphone bias voltage. The user guide recommends:
- Use AVDD (typically 3.3V) filtered through an RC low-pass filter (220 ohm + 1uF) as mic bias
- The bias resistors (2.2k typical) are placed between the filtered AVDD and each microphone terminal

For electret condenser microphones (ECM), the circuit provides bias through the AVDD supply rail with appropriate filtering.

For digital PDM microphones (DMIC), the ES8311's MIC1P pin doubles as a DMIC_SDA input. The BCLK/SCLK pin provides the DMIC clock. Bias comes from PVDD.

## 2.8 Sample Rate Support

| Mode | Sample Rate Range |
|------|------------------|
| Single Speed | 8 kHz, 11.025 kHz, 16 kHz, 22.05 kHz, 32 kHz, 44.1 kHz, 48 kHz |
| Double Speed | 64 kHz, 88.2 kHz, 96 kHz |

The ADC and DAC can operate at different sample rates if their internal clock dividers are configured independently.

**For voice applications on this board:** 16 kHz is the typical choice (good speech quality with low bandwidth). For music playback: 44.1 kHz or 48 kHz.

## 2.9 Arduino Library Options

### 1. arduino-audio-driver by pschatzmann (Recommended)
- **Repository:** https://github.com/pschatzmann/arduino-audio-driver
- **Supports:** ES8311, ES8388, AC101, CS43l22, ES7243, and many more
- **Features:** Unified API for multiple codec chips; handles I2C configuration and I2S setup
- **Install:** PlatformIO or manual

### 2. Waveshare's Example Code
- Located at: `examples/Arduino-v3.3.5/examples/15_ES8311/`
- Direct register manipulation using Wire library
- Demonstrates basic playback and recording

### 3. ESP-ADF Audio HAL (Arduino-compatible)
- The ESP-ADF (Audio Development Framework) includes a complete ES8311 driver
- Can be extracted and used as a standalone Arduino component

## 2.10 ESP-IDF Driver Options

### esp_codec_dev Component (Recommended by Espressif)
- **Registry:** https://components.espressif.com/components/espressif/esp_codec_dev
- **Version:** v1.4.0+
- **Features:** Unified codec API for ES8311, ES8388, and others
- **Usage:**
  ```c
  #include "es8311.h"
  #include "esp_codec_dev.h"

  // Configure I2C for codec control
  // Configure I2S for audio data
  // Initialize codec device
  const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(...);
  const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(...);
  const audio_codec_if_t *codec_if = es8311_codec_new(...);
  esp_codec_dev_cfg_t dev_cfg = { .codec_if = codec_if, ... };
  esp_codec_dev_handle_t dev = esp_codec_dev_new(&dev_cfg);
  ```

### Legacy espressif/es8311 Component (Deprecated)
- **Registry:** https://components.espressif.com/components/espressif/es8311 (v1.0.0)
- **Status:** Deprecated; use esp_codec_dev instead
- Implements I2C register configuration only; user must set up I2S separately

### ESP-ADF es8311 Driver
- **Source:** https://github.com/espressif/esp-adf/blob/master/components/audio_hal/driver/es8311/es8311.c
- Full-featured driver with initialization, volume control, mute, mic gain, power management
- Integrated with ESP-ADF audio pipeline framework

### ESP-IDF Example: i2s_es8311
- **Path:** `examples/peripherals/i2s/i2s_codec/i2s_es8311/`
- Demonstrates music playback and microphone echo
- Configurable: sample rate, bit depth, MCLK source

### Waveshare Official ESP-IDF Example
- Not present as a standalone example in the current repo, but the XiaoZhi firmware incorporates ES8311 support

## 2.11 Dual Digital Microphone Array on This Board

The Waveshare board is described as having a "dual digital microphone array." This is implemented using two PDM (Pulse Density Modulation) MEMS microphones. Here is how the architecture works:

**How it connects to the single-channel ES8311:**

The ES8311 has a digital microphone (DMIC) interface on pin MIC1P/DMIC_SDA. It uses the BCLK/SCLK signal as the DMIC clock. The ES8311 can select data from either the left or right channel of a PDM pair via the DMIC_SENSE register bit (0x15, bit 0):
- DMIC_SENSE = 0: Data latched on clock positive edge (left channel)
- DMIC_SENSE = 1: Data latched on clock negative edge (right channel)

**However, since the ES8311 is a mono codec, it can only process ONE microphone at a time through its ADC path.** For a dual-microphone setup, possible approaches include:
1. Use one mic through the ES8311's DMIC interface for voice capture
2. Use the second mic via the ESP32-C6's built-in I2S peripheral directly (for noise reference/beamforming)
3. Switch between mics in software

The dual-mic array enables:
- **Noise cancellation:** One mic captures the speaker's voice, the other captures ambient noise
- **Beamforming:** Directional audio pickup
- **Echo cancellation:** The reference signal from the speaker can be subtracted from the microphone signal

## 2.12 Voice Recognition and AI Assistant Usage

### XiaoZhi AI Framework Integration

The board is explicitly designed for the XiaoZhi AI voice assistant (compatible with v2.2.4+). The audio pipeline works as follows:

**Recording (Voice Input):**
1. User speaks into the onboard microphone(s)
2. ES8311 ADC digitizes the audio (typically 16 kHz, 16-bit)
3. ESP32-C6 reads audio data via I2S
4. Local wake-word detection runs on the ESP32-C6 (lightweight model)
5. After wake-word triggers, audio is streamed to the cloud server
6. Cloud performs Speech-to-Text (STT) using Whisper or similar
7. Cloud LLM (DeepSeek, Doubao, etc.) generates response text
8. Cloud performs Text-to-Speech (TTS)

**Playback (Voice Output):**
1. TTS audio arrives from cloud as compressed audio stream
2. ESP32-C6 decodes and sends data to ES8311 via I2S
3. ES8311 DAC converts to analog
4. External amplifier drives the onboard speaker

### Key ES8311 Settings for Voice AI

For optimal voice capture:
- **Sample rate:** 16 kHz (sufficient for speech, saves bandwidth)
- **Bit depth:** 16-bit (adequate for speech recognition)
- **PGA gain:** 21-27 dB for typical electret/MEMS mic sensitivity
- **ALC:** Enable with appropriate thresholds to handle varying speaking distances
- **High-pass filter:** Enable (ADC_HPF = 1) to remove DC offset and low-frequency noise

For playback:
- **Sample rate:** Match TTS output (typically 16-24 kHz for voice)
- **DAC volume:** Set via DAC_VOLUME register based on speaker characteristics
- **DRC:** Enable for consistent volume output through small speakers

### Digital Feedback for Echo Cancellation

The ES8311 has a built-in digital feedback feature (Register 0x44, ADCDAT_SEL) that can combine ADC and DAC data on the ASDOUT pin. This is useful for echo cancellation:
- ADCDAT_SEL = 0: ADC + ADC (default, normal)
- ADCDAT_SEL = 4: DACL + ADC (DAC loopback on left channel, ADC on right)
- ADCDAT_SEL = 5: ADC + DACR (ADC on left, DAC loopback on right)

This allows the ESP32-C6 to receive both the microphone signal and the speaker playback signal simultaneously, enabling acoustic echo cancellation (AEC) in software.

## 2.13 Power Consumption

| Mode | Condition | Typ | Unit |
|------|-----------|-----|------|
| Normal Operation | DVDD=1.8V, PVDD=1.8V, AVDD=3.3V | 8 | mA |
| Power Down | DVDD=1.8V, PVDD=1.8V, AVDD=3.3V | ~0 | uA |

**Power supply domains:**

| Supply | Min | Typ | Max | Unit |
|--------|-----|-----|-----|------|
| DVDD (digital) | 1.6 | 1.8/3.3 | 3.6 | V |
| PVDD (digital I/O) | 1.6 | 1.8/3.3 | 3.6 | V |
| AVDD (analog) | 1.7 | 1.8/3.3 | 3.6 | V |

**Power-down sequence (for sleep mode):**
1. Set all control bits in Register 0x0D to '1' (power down all analog blocks)
2. Set VMIDSEL to '00' (power down VMID)
3. Clear CSM_ON (Register 0x00, bit 7) to enter state machine power down

**Power-up sequence:**
1. Set reset bits in Register 0x00 (RST_DIG, RST_CMG, RST_MST, RST_ADC_DIG, RST_DAC_DIG)
2. Clear CSM_ON to power down state
3. Delay several milliseconds
4. Clear reset bits, set CSM_ON to '1'
5. Configure clocks, power up analog blocks via Register 0x0D

The 14 mW total (playback + record) is impressively low for a full codec, making it well-suited for the battery-powered wearable form factor.

---

# XiaoZhi AI Voice Assistant Integration

## What is XiaoZhi?

XiaoZhi (xiaozhi-esp32) is an open-source AI chatbot framework designed for ESP32 microcontrollers. It is available at https://github.com/78/xiaozhi-esp32 and is the primary AI voice assistant platform supported by this board.

## Architecture

XiaoZhi uses a hybrid on-device/cloud architecture:

| Task | Location | Technology |
|------|----------|-----------|
| Wake-word detection | On-device (ESP32-C6) | Lightweight neural network |
| Voice Activity Detection (VAD) | On-device | Energy-based or ML model |
| Audio capture & encoding | On-device | ES8311 ADC + Opus/PCM encoding |
| Speech-to-Text (STT) | Cloud server | Whisper, FunASR, etc. |
| Language Model (LLM) | Cloud server | DeepSeek, Doubao, Qwen, GPT, etc. |
| Text-to-Speech (TTS) | Cloud server | Edge-TTS, Coqui, etc. |
| Audio decode & playback | On-device | ES8311 DAC |
| Display UI | On-device | LVGL on AMOLED |
| MCP (Model Context Protocol) | Cloud server | Smart home control, tools |

## Key Features for This Board

- **No PSRAM required:** XiaoZhi is optimized to run on ESP32-C6 which has no PSRAM (only 512KB SRAM)
- **Wi-Fi 6 connectivity:** Low-latency audio streaming to cloud
- **Dual mic array:** Better voice capture quality
- **AMOLED display:** Shows conversation text, expressions, status
- **Battery powered:** Portable AI assistant form factor

## Audio Pipeline

```
[Microphone] --> [ES8311 ADC] --> [I2S] --> [ESP32-C6] --> [WiFi] --> [Cloud STT/LLM/TTS]
                                                                           |
[Speaker] <-- [Amplifier] <-- [ES8311 DAC] <-- [I2S] <-- [ESP32-C6] <-- [WiFi]
```

---

# Development Strategy for This Board

## I2C Bus Sharing

All I2C peripherals share a single bus. The address map:

| Device | Address | Function |
|--------|---------|----------|
| AXP2101 PMIC | 0x34 | Power management |
| FT3168 Touch | 0x38 | Touch input |
| PCF85063A RTC | 0x51 | Real-time clock |
| QMI8658 IMU | 0x6B | Motion sensing |
| ES8311 Codec | 0x18 | Audio control (data via I2S) |

No address conflicts exist. Use a single I2C master instance at 400 kHz.

## Pin Budget Considerations

The ESP32-C6 has limited GPIO. The audio codec requires 5-6 pins for I2S (MCLK, BCLK, WS, DOUT, DIN) plus the shared I2C (SDA, SCL). The RTC needs only the shared I2C plus one GPIO for the INT pin.

## Power-Optimized Usage

For a battery-powered wearable/AI assistant:

1. **Use RTC alarm for scheduled wake:** Set ESP32-C6 to deep sleep; PCF85063A draws only 220 nA while keeping time; alarm pulls INT low to wake the ESP32-C6

2. **Power down ES8311 during sleep:** Set all power-down bits in the codec before ESP32-C6 enters deep sleep (draws ~0 uA)

3. **Use AXP2101 to control power rails:** The PMIC can cut power to peripherals that are not needed

4. **Timer-based wakeup for periodic tasks:** Use the PCF85063A countdown timer (e.g., check for notifications every 15 minutes)

## Recommended Development Order

1. **RTC first:** Simple I2C read/write, verifiable with serial output, no additional interfaces needed
2. **ES8311 second:** Requires I2S setup in addition to I2C; test with simple tone generation then microphone echo
3. **XiaoZhi integration last:** Requires WiFi, cloud server, and full audio pipeline working together

---

## Source Documents

| Document | Reference |
|----------|-----------|
| PCF85063A Datasheet | NXP Rev.7, 30 March 2018 (https://files.waveshare.com/wiki/common/PCF85063A.pdf) |
| ES8311 Datasheet | Everest Semiconductor Rev.10.0, January 2021 (https://files.waveshare.com/wiki/common/ES8311.DS.pdf) |
| ES8311 User Guide | Everest Semiconductor Rev.1.11, September 2018 (https://files.waveshare.com/wiki/common/ES8311.user.Guide.pdf) |
| Waveshare Product Page | https://www.waveshare.com/esp32-c6-touch-amoled-1.8.htm |
| Waveshare GitHub | https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8 |
| XiaoZhi ESP32 | https://github.com/78/xiaozhi-esp32 |
| ESP Component: esp_codec_dev | https://components.espressif.com/components/espressif/esp_codec_dev |
| ESP Component: es8311 (deprecated) | https://components.espressif.com/components/espressif/es8311 |
| PCF85063A Arduino Library | https://github.com/SolderedElectronics/PCF85063A-Arduino-Library |
| arduino-audio-driver | https://github.com/pschatzmann/arduino-audio-driver |
