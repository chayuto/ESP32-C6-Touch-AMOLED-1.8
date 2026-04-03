Here’s what I can reliably extract for the ESP32‑C6‑Touch‑AMOLED‑1.8 board and each of the subsystems you listed, with pointers to the best official docs, schematics, and community code.

***
## Core board docs and repos
- Official Waveshare wiki (primary reference, includes Arduino/ESP‑IDF demos and library versions):  
  https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8 [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
- Spotpear mirror wiki (same content plus download links for demo packs, “Mylibrary” pin macros, etc.): [spotpear](https://spotpear.com/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)
  https://spotpear.com/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html [spotpear](https://spotpear.com/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)
- Official shop pages and 3rd‑party resellers summarising features and pinouts: [waveshare](https://www.waveshare.com/esp32-c6-touch-amoled-1.8.htm)
- GitHub “engineering sample program” repo (currently only a lightweight starter, no full pin_config.h):  
  https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8 [github](https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8)
- Schematic PDF (critical for pin mapping, I²C topology, power rails, IO expander wiring):  
  https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-1.8/ESP32-C6-Touch-AMOLED-1.8-Schematic.pdf [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)

XiaoZhi AI support for this board is tracked here:

- XiaoZhi AI application tutorial & supported‑board list (shows ESP32‑C6‑Touch‑AMOLED‑1.8 is supported, firmware v2.2.4): [docs.waveshare](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16/ESP32-AI-Tutorials)
- XiaoZhi main repo (board support, prebuilt firmware, but board‑specific header is not trivially accessible via search tools yet): [github](https://github.com/78/xiaozhi-esp32)

***
## 1. Pin configuration and schematic highlights
### Status of “pin_config.h” on Waveshare GitHub
The official Waveshare GitHub repo `waveshareteam/ESP32-C6-Touch-AMOLED-1.8` currently only exposes a short README and does not contain a `pin_config.h` file or equivalent board header in the public tree. [github](https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8)
Instead, the Arduino demo pack referenced in the wiki provides a `Mylibrary` folder with “board pin macro definition” headers, distributed as a ZIP download rather than via GitHub, so the exact header contents are not directly browsable online. [spotpear](https://spotpear.com/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)

Practically, the schematic PDF plus the wiki’s “Pinout Definition” section give the same information: which ESP32‑C6 GPIOs are tied to LCD QSPI, touch, IMU, RTC, audio, PMIC, the IO expander, and the external pads. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
### Key nets from the schematic
From the schematic netlist, the main functional groups are wired as follows: [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)

- **Main I²C bus (ESP32_SCL / ESP32_SDA)** connects to:
  - QMI8658A IMU (SCL/SDA, address comment “0x6B”). [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - PCF85063A RTC (SCL/SDA, INT pin to `RTC_INT`). [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - AXP2101 PMIC (TWSI SCK/SDA). [files.waveshare](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf)
  - TCA9554 IO expander (SCL/SDA). [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - ES8311 codec’s control interface (its SCL/SDA pins are also on this bus per user guide and ES8311 docs). [files.waveshare](https://files.waveshare.com/wiki/common/ES8311.user.Guide.pdf)
- **Display and touch:**
  - SH8601 AMOLED driver over QSPI: nets `LCD_CS`, `QSPI_SCL`, `QSPI_SIO0…3` from ESP32‑C6 GPIOs. [cnx-software](https://www.cnx-software.com/2025/12/27/esp32-c6-amoled-development-board-touch-display-built-in-mic-and-speaker-imu-rtc/)
  - FT3168 / FT6146 capacitive touch over I²C: nets `TP_SCL`, `TP_SDA`, with `TP_INT` and `TP_RESET` lines; reset and some control signals go through the IO expander (see section 5). [spotpear](https://spotpear.com/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)
- **Audio:**
  - ES8311 I²S lines: `I2S_MCLK`, `I2S_SCLK`, `I2S_LRCK`, `I2S_DSDIN` (to codec DAC input), `I2S_ASDOUT` (from codec ADC). [files.waveshare](https://files.waveshare.com/wiki/common/ES8311.user.Guide.pdf)
  - NS4150B speaker amplifier connected to ES8311 differential outputs `OUTP/OUTN` and to the on‑board 8 Ω speaker. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - Analog microphone connected as differential `MIC_P`/`MIC_N` into ES8311’s mic input pins. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
- **SD card and flash:**
  - External 16 MB QSPI NOR Flash `XM25QH128DHIQT` on the ESP32‑C6 SPI flash pins (`SPICLK`, `SPIQ`, `SPID`, `SPIHD`, `SPIWP`, `SPICS0`). [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - µSD socket on SDIO lines (`SDIO_CMD`, `SDIO_CLK`, `SDIO_DATA0…3`). [cnx-software](https://www.cnx-software.com/2025/12/27/esp32-c6-amoled-development-board-touch-display-built-in-mic-and-speaker-imu-rtc/)
- **Buttons and power:**
  - `BOOT` button to GPIO0 (boot‑strap / user button). [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - `PWR` / power key line from AXP2101 (`PWRON` and `PWROK` nets) into ESP32‑C6 reset/enable circuitry (`CHIP_PU`, GPIO18 through MOSFET). [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
- **User‑accessible pads:**
  - The wiki lists solder pads for UART, USB, I²C, BAT, 5 V, 3.3 V and several GPIOs on 1.5 mm pitch, matching nets such as `USB'_P`, `USB'_N`, `ESP32_SCL`, `ESP32_SDA`, and GPIO‑labelled nets in the schematic. [kamami](https://kamami.pl/en/esp32/1202986-esp32-c6-1-8inch-touch-amoled-display-development-board-368-448-resolution-supports-wi-fi-6-ble.html)

For full per‑pin mapping you will want to keep the schematic PDF open; it explicitly lists all 40 ESP32‑C6 module pins (GPIO0…23, SPI, SDIO, power) and their board‑level net names. [espressif](https://www.espressif.com/sites/default/files/documentation/esp32-c6-wroom-1_wroom-1u_datasheet_en.pdf)

***
## 2. AXP2101 PMIC – rails, battery, XPowersLib, deep sleep
### Power rails and battery path
AXP2101 is a highly integrated single‑cell Li‑ion PMU with 4 buck converters, 11 LDOs, NVDC battery charger, fuel gauge, and RTC LDO, intended for wearables and portable devices. [x-powers](http://www.x-powers.com/en.php/Info/product_detail/article_id/95)
On this board it provides the main 3.3 V rail (`VCC3V3`) via DCDC1, additional core rails (`0.9 V` and `1.2 V` from DCDC2/3/CPUSLDO), and a 1.8 V domain, plus analog LDOs for audio, RTC and other peripherals. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)

From the schematic and AXP2101 documentation: [files.waveshare](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf)

- **Buck converters:**
  - DCDC1 → `VCC3V3` (3.3 V system and IO). [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - DCDC2/CPUSLDO → ~0.9–1.2 V CPU core. [x-powers](http://www.x-powers.com/en.php/Info/product_detail/article_id/95)
  - DCDC3 / DCDC4 used for auxiliary rails (1.2 V, 1.8 V). [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
- **LDOs of interest:**
  - RTCLDO → `VCC‑RTC` net powering the PCF85063 and optionally a backup rail. [x-powers](http://www.x-powers.com/en.php/Info/product_detail/article_id/95)
  - ALDO1–4 used for 3.3 V / 1.8 V analog rails (`VL1_3.3V`, `VL2_3.3V`, `VL3_3V`, `VL3_1.8V`). [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - BLDO1/2 for 2.8 V domain (`VL_2.8V`), often used for RF or other analog loads. [x-powers](http://www.x-powers.com/en.php/Info/product_detail/article_id/95)
- **Battery and USB:**
  - `VBUS` from USB‑C routed into AXP2101; PMIC handles input OV protection and battery charging. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - `VBAT1/VBAT2` nets from the MX1.25 Li‑ion connector into PMIC CHG/BAT pins; charger supports ~3.9–5.5 V input and up to about 1.5 A charge current depending on configuration. [file.whycan](http://file.whycan.com/files/members/7106/AXP2101_Brief_202210.pdf)
  - `CHGLED` net available for charge indicator LED, and `TS` pin tied to an NTC for battery temperature monitoring (or disabled via XPowersLib). [file.whycan](http://file.whycan.com/files/members/7106/AXP2101_Datasheet_V1.4.pdf)
### Battery management and charging configuration
AXP2101’s charger supports programmable charge current (steps from ~100 mA up to 1 A), termination current and target voltage (e.g. 4.2 V), plus JEITA temperature‑aware charging via TS. [x-powers](http://www.x-powers.com/en.php/Info/product_detail/article_id/95)
Waveshare’s ESP‑IDF demo “01_AXP2101” uses a port of XPowersLib to read battery voltage, charge state and percentage via the internal fuel gauge and ADC channels. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Development-Environment-Setup-Arduino/)

XPowersLib example code for AXP2101 shows typical configuration patterns: [github](https://github.com/lewisxhe/XPowersLib/blob/master/examples/XPowersLibInterface_Example/XPowersLibInterface_Example.ino)

- Instantiate and init the PMU (defaults to I²C address 0x34 / 0x35 – 7‑bit 0x34 corresponds to TWSI 0x68). [files.waveshare](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf)
- Enable/disable individual rails via `setPowerChannelVoltage()` and `enablePowerOutput()/disablePowerOutput()` for DCDCx, ALDOx, BLDOx etc. [github](https://github.com/lewisxhe/XPowersLib/blob/master/examples/XPowersLibInterface_Example/XPowersLibInterface_Example.ino)
- Configure charge target voltage and current with `setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2)` and `setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_500MA)` (for example). [github](https://github.com/lewisxhe/XPowersLib/blob/master/examples/XPowersLibInterface_Example/XPowersLibInterface_Example.ino)
- Use the fuel‑gauge helpers (`fuelGaugeControl(true, true)`, `getBatteryPercent()`, `getBattVoltage()`, `isCharging()`, `isBatteryConnect()`) to monitor SoC. [github](https://github.com/lewisxhe/XPowersLib/blob/master/examples/XPowersLibInterface_Example/XPowersLibInterface_Example.ino)
### Using AXP2101 for deep‑sleep and power‑key behaviour
AXP2101 includes programmable “system power‑down voltage” (VSYS cutoff) and power‑key timing, which XPowersLib exposes as: [github](https://github.com/lewisxhe/XPowersLib/blob/master/examples/XPowersLibInterface_Example/XPowersLibInterface_Example.ino)

- `setSysPowerDownVoltage()` / `getSysPowerDownVoltage()` – set brown‑out cutoff (e.g. 2.6 V) so the PMIC turns the system off gracefully when the battery is low. [github](https://github.com/lewisxhe/XPowersLib/blob/master/examples/XPowersLibInterface_Example/XPowersLibInterface_Example.ino)
- `setPowerKeyPressOffTime()` / `setPowerKeyPressOnTime()` – define how long the user must hold the PWR key to power on or off (e.g. 4 s off, 128 ms on). [github](https://github.com/lewisxhe/XPowersLib/blob/master/examples/XPowersLibInterface_Example/XPowersLibInterface_Example.ino)

On this board the PWR button goes into AXP2101’s PWRON input and the PMIC’s `PWROK`/`CHIP_PU` signals gate the ESP32‑C6 enable line, so system‑level “deep off” can be implemented by letting AXP2101 cut rails while ESP32‑C6 is completely unpowered, and “deep sleep” by keeping rails on but using ESP32‑C6 low‑power modes. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)

***
## 3. QMI8658 IMU – specs, motion/tap, auto‑rotation, steps
### Core specifications
The QMI8658A/C is a 6‑axis IMU (3‑axis accelerometer + 3‑axis gyroscope) supporting both I²C/I³C and SPI, targeted at high‑performance consumer and navigation applications. [xpulabs.github](https://xpulabs.github.io/products/amnos/sensor/cic_sen0002_qmi8658.html)
Key specs from the QST datasheet and breakout documentation: [qstcorp](https://www.qstcorp.com/en_imu_prod/QMI8658)

- Accelerometer:
  - Ranges: ±2 g, ±4 g, ±8 g, ±16 g (selectable). [jlcpcb](https://jlcpcb.com/partdetail/QST-QMI8658A/C3021082)
  - Resolution: 16‑bit, noise around 200 µg/√Hz, temperature range −40 to +85 °C. [uploadcdn.oneyac](https://uploadcdn.oneyac.com/attachments/files/brand_pdf/qst/17/D4/QMI8658A.pdf)
- Gyroscope:
  - Ranges: ±16 to ±2048 °/s (16/32/64/128/256/512/1024/2048 °/s). [cnx-software](https://www.cnx-software.com/2025/12/27/esp32-c6-amoled-development-board-touch-display-built-in-mic-and-speaker-imu-rtc/)
  - Resolution: 16‑bit; noise density about 15 mdps/√Hz. [files.waveshare](https://files.waveshare.com/upload/5/5f/QMI8658A_Datasheet_Rev_A.pdf)
- Features:
  - 1536‑byte FIFO for buffering sensor data to reduce host wakeups. [xpulabs.github](https://xpulabs.github.io/products/amnos/sensor/cic_sen0002_qmi8658.html)
  - Motion‑on‑Demand and built‑in XKF3 6/9‑axis sensor fusion with typical orientation accuracy ±3° pitch/roll, ±5° yaw. [qstcorp](https://www.qstcorp.com/en_imu_prod/QMI8658)

The schematic shows QMI8658A (U5) powered from 3.3 V with I²C SDA/SCL tied to the main ESP32_SDA/SCL nets and address pin configured such that its 7‑bit I²C address is 0x6B. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
### Motion/tap, auto‑rotation, pedometer
The QMI8658 supports various motion‑triggered wake functions (through interrupt pins INT1/INT2) and low‑power motion‑on‑demand mode described in the datasheet; these are commonly used for: [xpulabs.github](https://xpulabs.github.io/products/amnos/sensor/cic_sen0002_qmi8658.html)

- Basic motion/tilt detection to wake the MCU out of sleep or rotate the UI.
- Step counting and gesture detection when combined with XKF3 fusion/pedometer algorithms or host‑side algorithms, which several breakout and board vendors explicitly advertise. [spotpear](https://spotpear.com/wiki/ESP32-S3-1.8-inch-AMOLED-Display-TouchScreen-AI-Speaker-Microphone-Programmable-Watch-DeepSeek.html)

On this board:

- INT1 and INT2 are routed to ESP32‑C6 GPIOs labelled `QMI_INT1` and `QMI_INT2`, so you can map motion events to GPIO interrupts in ESP‑IDF. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
- The Arduino/ESP‑IDF demos referred to as `11_LVGL_QMI8658_ui` show how the board uses SensorLib to initialise the IMU and feed acceleration into LVGL for drawing graphs and orientation handling. [spotpear](https://spotpear.com/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)

Auto‑rotation is achieved at the application level: you read accelerometer gravity vector, map it to screen orientation, and call `lv_disp_set_rotation()` (LVGL) or similar; the board demos already implement this behaviour using QMI8658 data and LVGL. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Development-Environment-Setup-Arduino/)

***
## 4. PCF85063 RTC + ES8311 audio
### RTC (PCF85063A) – alarm, wakeup, backup supply
PCF85063A is a tiny I²C real‑time clock/calendar that tracks seconds to years, with alarm, countdown timer, watchdog and a clock‑out pin; typical 7‑bit I²C address is 0x51. [lv.mouser](https://lv.mouser.com/new/nxp-semiconductors/nxp-pcf85063-clocks/)
It is optimised for very low power (≈0.2 µA at 3 V) and supports 400 kbit/s I²C, making it suitable to run continuously from a coin cell or RTCLDO while the rest of the system sleeps. [nxp](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf)

Board‑level details from the schematic and wiki: [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)

- 32.768 kHz crystal Y1 connects to OSCI/OSCO with 22 pF load capacitors. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
- VDD is tied to `VCC‑RTC` from AXP2101’s RTCLDO; `VBACKUP` of AXP2101 and separate backup‑battery pads are also present so RTC can run while the main Li‑ion is swapped. [spotpear](https://spotpear.com/wiki/ESP32-S3-1.8-inch-AMOLED-Display-TouchScreen-AI-Speaker-Microphone-Programmable-Watch-DeepSeek.html)
- INT output goes to `RTC_INT`, which is routed to an ESP32‑C6 GPIO (visible in the ESP32 netlist, typically GPIO7 or another free IO), so alarms or periodic interrupts can wake the MCU from deep sleep. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Development-Environment-Setup-Arduino/)

Waveshare’s Arduino examples like `05_GFX_PCF85063_simpleTime` and LVGL demo `10_LVGL_PCF85063_simpleTime` use SensorLib to set/read time and show it on screen, demonstrating basic RTC operation. [spotpear](https://spotpear.com/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)
### ES8311 codec – I²S config, mic/speaker path
ES8311 is a low‑power mono audio codec with 24‑bit ADC/DAC (8–96 kHz) and I²S/PCM interface; typical SNR is about 100 dB (ADC) and 110 dB (DAC) with THD+N better than −80 dB. [components.espressif](https://components.espressif.com/components/espressif/es8311/versions/1.0.0?language=en)
It uses I²C for register control (default 7‑bit address 0x18 or 0x19 depending on CE pin; many ESP‑IDF drivers default to 0x18 or 0x30 as 8‑bit). [esp32](https://www.esp32.com/viewtopic.php?t=35874)

On this board: [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)

- I²S lines:
  - `I2S_MCLK` to ES8311 MCLK pin. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - `I2S_SCLK` (bit clock), `I2S_LRCK` (word‑select), `I2S_DSDIN` (DAC input) and `I2S_ASDOUT` (ADC output) wired from ESP32‑C6 GPIOs. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
- Analog:
  - Single analog microphone on `MIC_P`/`MIC_N` into ES8311’s mic input; there is not a true digital mic array on this board (unlike some S3/1.43‑inch variants with dual digital mics). [cnx-software](https://www.cnx-software.com/2025/12/27/esp32-c6-amoled-development-board-touch-display-built-in-mic-and-speaker-imu-rtc/)
  - ES8311’s differential headphone/speaker outputs `OUTP/OUTN` go through AC‑coupling and then into NS4150B Class‑D amp, which drives an 8 Ω on‑board speaker. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
- Control:
  - ES8311 I²C SCL/SDA are on the common ESP32_SCL/SDA bus. [components.espressif](https://components.espressif.com/components/espressif/es8311/versions/1.0.0?language=en)
  - `Codec_CE` pin is tied to an ESP32‑C6 GPIO via a pull‑resistor network, allowing software to power‑cycle or reset the codec. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)

The Waveshare Arduino demo `13_ES8311` configures ESP32 I²S with appropriate pins/sample rate and calls `es8311_codec_init()` from an ESP‑IDF/Arduino ES8311 driver to play PCM audio, which is the best reference for specific register configuration on this board. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Development-Environment-Setup-Arduino)
Espressif’s official `espressif/es8311` component documents that it supports standard I²S modes (I²S, left‑justified, DSP) and requires you to initialise the ESP32 I²S peripheral separately, then use I²C writes via the driver to set format, volume, power‑down, etc. [components.espressif](https://components.espressif.com/components/espressif/es8311/versions/1.0.0?language=en)
### XiaoZhi AI, audio path, and mic usage
The XiaoZhi AI firmware for this board uses the ES8311 and microphone as the front end for wake‑word detection, streaming audio to an AI model (DeepSeek, Qwen, etc.) and playing back TTS via the speaker. [docs.waveshare](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16/ESP32-AI-Tutorials)
The XiaoZhi docs treat the audio subsystem as a standard single‑mic, mono output chain (I²S + ES8311) and provide board‑specific firmware builds (`waveshare-esp32-c6-touch-amoled-1.8`) but do not yet expose a simple board config header for you to reuse, so the schematic and Waveshare demos remain the authoritative reference for low‑level audio pin mapping. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)

***
## 5. TCA9554 IO expander and I²C topology
### I²C topology and device addresses
All board‑level I²C devices share the same bus nets `ESP32_SCL` and `ESP32_SDA` from the ESP32‑C6, which are also broken out on pads for external use. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
Based on datasheets and board comments, the likely 7‑bit I²C addresses are:

- **QMI8658A IMU:** 0x6B (SDO/SA0 pulled high; schematic explicitly annotates “0X6B”). [qstcorp](https://www.qstcorp.com/en_imu_prod/QMI8658)
- **PCF85063A RTC:** 0x51 (fixed by NXP, no address pins). [lv.mouser](https://lv.mouser.com/new/nxp-semiconductors/nxp-pcf85063-clocks/)
- **ES8311 codec:** 0x18 or 0x19 depending on CE wiring; Espressif’s `espressif/es8311` component uses a default (`ES8311_ADDR`/`ES8311_CODEC_DEFAULT_ADDR`) consistent with CE=0x18/0x30 (8‑bit). [components.espressif](https://components.espressif.com/components/espressif/es8311/versions/1.0.0?language=en)
- **AXP2101 PMIC:** TWSI slave default 8‑bit addresses 0x68/0x69, corresponding to 7‑bit 0x34/0x35; many higher‑level drivers (CircuitPython, ESPHome) use 0x34. [docs.nanoframework](https://docs.nanoframework.net/devices/Iot.Device.Axp2101.Axp2101.html)
- **TCA9554 IO expander:** base 0x20–0x27 range; exact address is determined by A0–A2 wiring.  
  The schematic shows A0–A2 connected via resistors R21/R18/R22 to VCC3V3/GND, but the text export does not clearly show which are pulled high vs. low, so it is safest to confirm with an I²C scan (the device will appear at one of 0x20–0x27). [ti](https://www.ti.com/product/TCA9554)
- **FT3168/FT6146 touch controller:** runs on a separate pair `TP_SCL`/`TP_SDA` still driven by the ESP32, and will be at its own fixed address (typical FT3168 is 0x38); examples in related boards (S3 AMOLED) use 0x38 as well. [spotpear](https://spotpear.com/wiki/ESP32-S3-1.8-inch-AMOLED-Display-TouchScreen-AI-Speaker-Microphone-Programmable-Watch-DeepSeek.html)

In practice, the Arduino/ESP‑IDF examples disable I²C scanning and hard‑code addresses in the respective libraries (SensorLib, XPowersLib, ES8311 driver, Adafruit_XCA9554), so checking those sketches is a good confirmation step. [spotpear](https://spotpear.com/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)
### TCA9554 IO expander pin map and usage
TCA9554 is an 8‑bit I²C IO expander (P0–P7) with 1.65–5.5 V supply and interrupt output, allowing you to add eight extra GPIOs controllable over I²C. [ti](https://www.ti.com/product/TCA9554)
On this board (U1): [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)

- Its VCC is tied to 3.3 V, SCL/SDA to ESP32_SCL/SDA, and it has an on‑board 100 nF decoupling capacitor. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
- P0–P7 are labelled `EXIO0…EXIO7`, with some lines dedicated to on‑board functions and others routed to external pins:
  - Several EXIO lines drive system control signals such as `SYS_OUT`, `PA_CTRL` (amplifier enable), `TP_RESET` (touch reset), and `LCD_RESET` (display reset) through 10 k resistors. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - Remaining EXIO lines are broken out as user‑accessible “EXIO” pins on headers, similar to EXIO1–EXIO7 on the ESP32‑C6‑Pico board described in Waveshare’s Pico docs. [waveshare](https://www.waveshare.com/wiki/ESP32-C6-Pico)

Adafruit’s `Adafruit_XCA9554` library is used by the Arduino demos (e.g. `02_Drawing_board`, `04_GFX_FT3168_Image`, `12_LVGL_Widgets`) to configure TCA9554 pins as outputs/inputs and to control reset/interrupt lines for the AMOLED and touch without burning more ESP32‑C6 GPIOs. [spotpear](https://spotpear.com/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)
The EXIO programming model mirrors the TCA9554PWR use on the ESP32‑C6‑Pico: set modes via configuration registers, write output states, and read input levels, with helper functions in the Waveshare “EXIO control demo”. [spotpear](https://spotpear.com/wiki/ESP32-C6-MINI-1-Pico-Bluetooth-5-WiFi-6-Zigbee-Thread.html)

***
## 6. Recommended official docs, libraries, and community references
To work effectively with each subsystem, these are the most relevant resources:

- **Board‑level:**
  - Waveshare wiki (features, pinout, Arduino + ESP‑IDF demos, library versions). [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8)
  - Spotpear English and Chinese wikis, often updated with demo packs and XiaoZhi‑specific instructions. [spotpear](https://spotpear.com/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)
  - Schematic PDF and (if available from the wiki) PCB/3D files. [files.waveshare](https://files.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-1.8/ESP32-C6-Touch-AMOLED-1.8-Schematic.pdf)
  - CNX Software overview article summarising specs and typical applications. [cnx-software](https://www.cnx-software.com/2025/12/27/esp32-c6-amoled-development-board-touch-display-built-in-mic-and-speaker-imu-rtc/)
- **AXP2101 / power:**
  - X‑Powers official datasheet and brief: AXP2101 Single Cell NVDC PMU with E‑gauge. [x-powers](http://www.x-powers.com/en.php/Info/product_detail/article_id/95)
  - XPowersLib GitHub repo (Arduino/ESP‑IDF PMIC driver) and AXP2101 interface example. [github](https://github.com/HwzLoveDz/AXP2101-PMIC)
  - ESP‑IDF demo description `01_AXP2101` in Waveshare docs for this board. [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Development-Environment-Setup-Arduino/)
- **QMI8658 IMU:**
  - QST QMI8658C/A product page and datasheets (specs, registers, motion‑on‑demand, FIFO). [qstcorp](https://www.qstcorp.com/en_imu_prod/QMI8658)
  - XPU Labs breakout documentation (good human‑friendly feature summary). [xpulabs.github](https://xpulabs.github.io/products/amnos/sensor/cic_sen0002_qmi8658.html)
  - JLCPCB part page with key parameters and links. [jlcpcb](https://jlcpcb.com/partdetail/QST-QMI8658A/C3021082)
  - Waveshare / Spotpear examples `03_QMI8658`/`11_LVGL_QMI8658_ui` in their C6/S3 AMOLED board wikis. [spotpear](https://spotpear.com/wiki/ESP32-S3-1.8-inch-AMOLED-Display-TouchScreen-AI-Speaker-Microphone-Programmable-Watch-DeepSeek.html)
- **PCF85063 RTC:**
  - NXP PCF85063A datasheet and Mouser RTC overview (alarms, watchdog, timers, current). [lv.mouser](https://lv.mouser.com/new/nxp-semiconductors/nxp-pcf85063-clocks/)
  - ESPHome PCF85063 component docs showing default 0x51 address. [esphome](https://esphome.io/components/time/pcf85063/)
  - Waveshare SensorLib (supports PCF85063 and QMI8658). [spotpear](https://spotpear.cn/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)
- **ES8311 audio:**
  - Everest ES8311 datasheet and Waveshare ES8311 user guide. [radiolocman](https://www.radiolocman.com/datasheet/pdf.html?di=168333)
  - Espressif `espressif/es8311` ESP‑IDF component documentation. [components.espressif](https://components.espressif.com/components/espressif/es8311/versions/1.0.0?language=en)
  - Waveshare Arduino demo `13_ES8311` and ESP‑IDF “Audio_Test” in 1.43‑inch and C6 AMOLED series docs (identical codec and very similar wiring). [docs.waveshare](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-1.8/Development-Environment-Setup-Arduino)
- **TCA9554 IO expander:**
  - TI TCA9554 datasheets and app notes (addressing, registers, polarity, interrupt). [ti](https://www.ti.com/product/TCA9554)
  - Adafruit_XCA9554 Arduino library and examples used by Waveshare. [spotpear](https://spotpear.com/wiki/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)
  - Waveshare ESP32‑C6‑Pico and EXIO demos showing the same EXIO programming model. [waveshare](https://www.waveshare.com/wiki/ESP32-C6-Pico)
- **XiaoZhi AI ecosystem:**
  - XiaoZhi‑ESP32 GitHub repo (`78/xiaozhi-esp32`), which includes support for `waveshare-esp32-c6-touch-amoled-1.8` in recent releases. [github](https://github.com/78/xiaozhi-esp32)
  - Waveshare XiaoZhi AI tutorial (firmware flashing, supported boards, and pointers to firmware repos). [docs.waveshare](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16/ESP32-AI-Tutorials)
  - Spotpear “ESP32‑C6 AI Voice Chat Robot for Xiaozhi 1.8‑inch AMOLED” product page (explains intended AI‑voice use cases and packaging). [spotpear](https://spotpear.com/shop/ESP32-C6-1.8-inch-AMOLED-Display-Capacitive-TouchScreen-WIFI6-Deepseek.html)

***

If you want, I can next turn this into an explicit pin table (ESP32‑C6 GPIO → net → peripheral) derived from the schematic, or sketch example ESP‑IDF init code for any of the subsystems (AXP2101 via XPowersLib, QMI8658 via SensorLib, ES8311 via Espressif component, or TCA9554 via Adafruit_XCA9554).