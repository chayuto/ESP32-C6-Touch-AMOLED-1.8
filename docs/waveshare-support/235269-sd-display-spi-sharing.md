# Ticket #235269 — SD card + AMOLED display SPI2_HOST sharing

- **Ticket ID:** #235269
- **Board:** ESP32-C6-Touch-AMOLED-1.8
- **Channel:** support@waveshare.com
- **Replying engineer:** 赵玉娇 (Zhao Yujiao), Waveshare technical support
- **Opened:** 2026-04-05
- **Last reply:** 2026-04-18
- **Status:** Closed — vendor confirmed the constraint after escalation
- **Outcome:** After two round-trips answering at the pin/CS level, Waveshare
  confirmed on 2026-04-14 that "the screen and the SD card on this development
  board cannot be used simultaneously. When you need to access one resource,
  you must first release the other." This matches our analysis and validates
  the runtime bus-switching approach used in
  `shared/components/amoled_driver` and `projects/16_bitchat_relay`. A
  follow-up asking whether the sibling ESP32-**S3**-Touch-AMOLED-1.8 has the
  same limitation confirmed that the S3 board does not: the S3 has two
  user-available SPI hosts (SPI2 + SPI3) and a real SDMMC controller, so the
  SD card and QSPI display can be on separate buses.
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

### 5. Us → Waveshare — 2026-04-12 (05:51 UTC)

> Hello 赵玉娇,
>
> Thank you for the replies. I think there is a misunderstanding about the
> level of the question, so let me restate it clearly.
>
> Our concern is not about GPIO pin conflicts or chip-select routing. We
> understand those are fine.
>
> Our concern is about the SPI peripheral controller inside the ESP32-C6 chip.
> On ESP32-C6, QSPI and standard 1-bit SPI are **not separate hardware
> blocks** — they are two modes of the same GPSPI2 controller. This is
> confirmed in the official ESP-IDF source code:
>
> - `components/soc/esp32c6/include/soc/soc_caps.h`:
>   `#define SOC_SPI_PERIPH_NUM 2`
> - `components/hal/include/hal/spi_types.h`: `SPI3_HOST` does not exist on
>   ESP32-C6.
>
> So the AMOLED display driver and the SD card driver must both use
> `SPI2_HOST`, and only one can hold the bus at a time.
>
> Could you please answer these specific questions:
>
> 1. In your official factory firmware, does the code call
>    `spi_bus_free(SPI2_HOST)` (or similar) to release the display bus before
>    accessing the SD card?
> 2. Or does the factory firmware only access the SD card before the display
>    is initialized?
> 3. Which source file in the `waveshareteam/ESP32-C6-Touch-AMOLED-1.8`
>    GitHub repository contains the SD card + display handling code?
>
> If simultaneous SD + display access is not supported by the factory
> firmware, please confirm this directly, and we will proceed with our own
> runtime bus-switching implementation.

### 6. Waveshare → Us — 2026-04-14 (赵玉娇, 10:34 UTC)

> Hello, we have confirmed that the screen and the SD card on this
> development board cannot be used simultaneously. When you need to access
> one resource, you must first release the other.

### 7. Us → Waveshare — 2026-04-17 (03:31 UTC)

> Hi,
>
> Thank you for the confirmation. I really like the board but it seems that
> I need a new hardware choice.
>
> Can you confirm that this
> <https://www.waveshare.com/ESP32-S3-Touch-AMOLED-1.8.htm> will NOT have the
> same issue?

### 8. Waveshare → Us — 2026-04-18 (赵玉娇, 06:12 UTC)

> The ESP32-S3-Touch-AMOLED-1.8 supports this.
>
> The ESP32-S3 integrates four SPI channels, with SPI0 and SPI1 occupied by
> the internal FLASH, while SPI2 and SPI3 are user-defined.
>
> SD cards and QSPI screens can share the same SPI bus, time-division
> multiplexing via the CS chip select signal. This is perfectly feasible for
> scenarios with low time sensitivity (such as writing to the SD card first
> and then reading from the display). However, for scenarios requiring
> simultaneous high-frequency operation of both the SD card and the screen
> (such as a music player simultaneously refreshing the UI and reading audio
> streams), the two devices will block each other, causing noticeable
> stuttering because only one device can be served on the same SPI bus at a
> time. Using the ESP32-S3, the SD card and screen can be assigned to SPI2
> and SPI3 respectively, fundamentally solving this problem. Furthermore, the
> S3's SD card can use SDMMC, resulting in faster read speeds.

---

## Assessment

The first two Waveshare replies answered at the pin/chip-select level, despite
the follow-up explicitly re-framing the question at the SPI peripheral host
level. The third reply (#6, after we cited ESP-IDF source) finally confirmed
the constraint directly: "the screen and the SD card on this development
board cannot be used simultaneously. When you need to access one resource,
you must first release the other." That matches our analysis.

What the vendor never answered:

- Questions 1-3 from message #5 (whether the factory firmware calls
  `spi_bus_free`, whether it avoids the problem by only doing SD I/O before
  display init, and which GitHub source file shows the pattern) were not
  addressed. The confirmation is operationally correct but does not tell us
  how the factory code does it.

The initial "QSPI and SPI are different, CS selects the device" framing
remains **wrong for ESP32-C6**:

- QSPI (4-bit) and standard 1-bit SPI are *modes of the same SPI2
  controller*, not separate peripherals.
- `SPI1_HOST` is reserved for internal flash; `SPI3_HOST` does not exist on
  C6 — `SPI2_HOST` is the only general-purpose SPI host available.
- A single SPI host binds to one `spi_bus_config_t` at a time. The QSPI
  display driver configures SPI2 with four data lines; SDSPI configures it
  with one. Both drivers cannot hold the host simultaneously regardless of
  chip-select routing.

The S3 reply (#8) is internally consistent and matches Espressif's SoC
documentation: ESP32-S3 exposes SPI2 and SPI3 as user-available hosts, plus
a real SDMMC controller, so the ESP32-S3-Touch-AMOLED-1.8 can run SD and
display on separate buses without time-multiplexing. It is also a useful
confirmation that Waveshare understands the underlying constraint — the
vendor's S3 answer frames the C6 situation correctly ("only one device can be
served on the same SPI bus at a time"), which is exactly what we were asking
about two replies earlier.

## Lessons for future correspondence

1. **State the level of the question up front.** Instead of "SPI2_HOST
   sharing," spell out "both drivers must call `spi_bus_initialize` on the
   same SPI host with different bus configs — how is this resolved?" This
   makes it harder for support to default to a pin-level answer.
2. **Cite ESP-IDF source when a pin-level answer comes back.** Quoting
   `soc_caps.h` and `spi_types.h` (message #5) is what moved the thread from
   "no conflict" to a direct confirmation. Vague follow-ups got vague
   answers; specific follow-ups got specific answers.
3. **Expect "cannot be used simultaneously" as the final answer, not a source
   pointer.** Even after escalation, Waveshare did not point to the factory
   firmware file that handles the switch. For implementation-level detail,
   go to `waveshareteam/ESP32-C6-Touch-AMOLED-1.8` on GitHub directly.
4. **Waveshare does correctly understand the constraint — it just takes
   rewording to get there.** The S3 reply proves they know "only one device
   can be served on the same SPI bus at a time." First-line defaults to a
   pin-level answer, but the underlying knowledge is there.
