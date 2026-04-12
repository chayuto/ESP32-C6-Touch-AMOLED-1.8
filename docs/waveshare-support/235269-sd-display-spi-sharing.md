# Ticket #235269 — SD card + AMOLED display SPI2_HOST sharing

- **Ticket ID:** #235269
- **Board:** ESP32-C6-Touch-AMOLED-1.8
- **Channel:** support@waveshare.com
- **Replying engineer:** 赵玉娇 (Zhao Yujiao), Waveshare technical support
- **Opened:** 2026-04-05
- **Last reply:** 2026-04-10
- **Status:** Closed without clear resolution
- **Outcome:** Vendor did not confirm the runtime-multiplexing approach. Their
  replies addressed the pin/chip-select level, not the SPI peripheral host
  level we asked about. We proceeded with time-multiplexing based on ESP-IDF
  documentation — see `18-sd-display-spi-sharing.md` for the technical
  analysis.
- **Related docs:** [`18-sd-display-spi-sharing.md`](../../18-sd-display-spi-sharing.md),
  [`07-complete-gpio-and-io-map.md`](../../07-complete-gpio-and-io-map.md)

---

## Transcript (chronological)

### 1. Us → Waveshare — 2026-04-05

> We're working on a project using the ESP32-C6-Touch-AMOLED-1.8 that needs to
> write to the SD card while the AMOLED display is active. From the schematic
> we can see the SD card is wired to GPIO 6/10/11/18 and accessed via SPI mode
> (D1/D2/D3 are unconnected), and the display uses QSPI on SPI2_HOST — the
> only user-available SPI bus on the ESP32-C6. Since the C6 only has an SDIO
> slave controller (no SDMMC host), it appears there's no way to access both
> peripherals concurrently. Can you confirm that simultaneous display + SD
> card operation is not possible on this board, and that time-multiplexing
> SPI2 (temporarily releasing the display bus to do SD I/O) is the intended
> approach?

### 2. Waveshare → Us — 2026-04-08 (赵玉娇, 11:26 UTC)

> Hello, this development board's screen uses a QSPI communication interface,
> and the SD card supports SPI access. There is no conflict issue.

### 3. Us → Waveshare — 2026-04-08 (20:43 UTC)

> Thank you for the clarification. We understand there is no hardware pin
> conflict — the display uses GPIO 0-5 and the SD card uses GPIO 6/10/11/18,
> so no pins are shared.
>
> Our concern is at the SPI peripheral level: on the ESP32-C6, both the QSPI
> display and the SD card (in SPI mode) must use SPI2_HOST — the only
> user-available SPI peripheral. Since they cannot both hold the bus
> simultaneously, we are using runtime bus switching: releasing SPI2 from the
> display driver before SD operations, then reclaiming it afterward.
>
> Could you confirm this is the intended usage pattern? Specifically:
> 1. Is runtime SPI2_HOST time-multiplexing between the QSPI display and SD
>    card the correct approach?
> 2. Does your official firmware handle this, or is it left to the user to
>    implement?

### 4. Waveshare → Us — 2026-04-10 (赵玉娇, 03:38 UTC)

> The screen uses a QSPI interface and does not use SPI, but the SD card can
> communicate normally via SPI. Even if both devices use SPI communication,
> they will select which device to communicate with via the chip select pin.
> The factory firmware screen uses QSPI communication, while the SD card uses
> SPI communication.

---

## Assessment

Waveshare answered at the pin/chip-select level both times, despite the
follow-up explicitly re-framing the question at the SPI peripheral host level.

The reply "QSPI and SPI are different, CS selects the device" is **incorrect
for ESP32-C6**:

- QSPI (4-bit) and standard 1-bit SPI are *modes of the same SPI2 controller*,
  not separate peripherals.
- `SPI1_HOST` is reserved for internal flash; `SPI3_HOST` does not exist on
  C6 — `SPI2_HOST` is the only general-purpose SPI host available.
- A single SPI host binds to one `spi_bus_config_t` at a time. The QSPI
  display driver configures SPI2 with four data lines; SDSPI configures it
  with one. Both drivers cannot hold the host simultaneously regardless of
  chip-select routing.

Chip-select gating is the right mental model when multiple devices share one
bus *configuration* (e.g. two standard SPI sensors on SPI2). It is not the
right mental model when two drivers want incompatible bus configurations on
the same host.

## Lessons for future correspondence

1. **State the level of the question up front.** Instead of "SPI2_HOST
   sharing," spell out "both drivers must call `spi_bus_initialize` on the
   same SPI host with different bus configs — how is this resolved?" This
   makes it harder for support to default to a pin-level answer.
2. **Ask for ESP-IDF-level specifics.** Request confirmation that the factory
   firmware calls `spi_bus_free` between display and SD operations, or a
   pointer to the relevant source file in `waveshareteam/ESP32-C6-Touch-AMOLED-1.8`.
3. **Don't expect escalation.** For SoC-internals questions, go directly to
   ESP-IDF docs and Espressif GitHub issues. Waveshare support is the right
   channel for schematic, BOM, and board-level questions only.
