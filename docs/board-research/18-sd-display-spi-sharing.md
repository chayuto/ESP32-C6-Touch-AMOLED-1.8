# SD Card + AMOLED Display: SPI2_HOST Sharing

Reference note on the shared-SPI-host constraint between the SH8601 QSPI AMOLED
and the SD card on the ESP32-C6-Touch-AMOLED-1.8, plus a record of the Waveshare
support exchange that tried (and failed) to clarify it.

---

## 1. The Constraint

The ESP32-C6 has exactly one user-available general-purpose SPI peripheral:
`SPI2_HOST`. `SPI1_HOST` is reserved for internal flash, and `SPI3_HOST` does
not exist on C6. The chip has no SDMMC host — only an SDIO *slave* controller —
so the SD card cannot use a dedicated SD host and must run in SPI mode on
`SPI2_HOST`.

Both peripherals on this board are therefore bound to the same SPI host:

| Peripheral    | Mode        | Host       | Pins                          |
|---------------|-------------|------------|-------------------------------|
| SH8601 AMOLED | QSPI (1-4)  | SPI2_HOST  | SCLK=0, D0=1, D1=2, D2=3, D3=4, CS=5 |
| SD card       | SPI (1-bit) | SPI2_HOST  | CLK=11, CMD=10, DATA=18, CS=6 |

There is **no GPIO conflict** — the pin sets are disjoint. The conflict is at
the *peripheral host* level: a single `spi_bus_config_t` can only describe one
bus topology at a time, and the ESP-LCD QSPI driver configures SPI2 with four
data lines while the SDSPI driver configures it with one. You cannot have both
drivers hold the host simultaneously.

## 2. Consequences

- **Concurrent access is impossible.** You cannot stream pixels to the display
  while reading/writing the SD card.
- **Runtime time-multiplexing works.** Release the host from the display
  driver (`spi_bus_remove_device` + `spi_bus_free`), initialize SDSPI, do SD
  I/O, tear SDSPI down, then reinitialize the display bus. Concrete
  implementation in this repo:
  - `shared/components/amoled_driver/src/amoled.c:251` — `amoled_release_spi()`
  - `shared/components/amoled_driver/src/amoled.c:276` — `amoled_reclaim_spi()`
  - `projects/16_bitchat_relay/main/main.c:73` — state machine showing the
    DISPLAY → RELEASED → SD_ACTIVE → RELEASED → DISPLAY transitions in
    production use.
- **Boot-time batch access is simpler.** If SD access is only needed to load
  assets once, do it *before* `amoled_init()` and never switch again.
- **Display will be frozen** (no refreshes, no partial updates) for the entire
  duration that the SD card holds the bus. Plan UI state accordingly — e.g.
  show a static "Loading…" frame that was pushed before the handoff.

## 3. Waveshare Support Exchange

We contacted Waveshare (ticket **#235269**, replying engineer **赵玉娇 / Zhao
Yujiao**, 2026-04-05 → 2026-04-18) to confirm the intended usage pattern. The
first two replies addressed the pin/chip-select level, not the SPI peripheral
host level we asked about. After we quoted ESP-IDF source (`soc_caps.h` /
`spi_types.h`) to pin the question to SoC internals, support confirmed
directly on 2026-04-14:

> "The screen and the SD card on this development board cannot be used
> simultaneously. When you need to access one resource, you must first
> release the other."

This matches the time-multiplexing approach implemented in
`amoled_release_spi` / `amoled_reclaim_spi` and used by project 16. A
follow-up also confirmed that the sibling **ESP32-S3-Touch-AMOLED-1.8** does
not have this limitation — the S3 exposes two user-available SPI hosts
(SPI2 + SPI3) and a real SDMMC controller, so SD and QSPI display can live on
separate buses.

Full verbatim transcript and assessment:
[`docs/waveshare-support/235269-sd-display-spi-sharing.md`](docs/waveshare-support/235269-sd-display-spi-sharing.md).

**Takeaway for future C6 SPI questions:** first-line support defaults to
pin-level answers. To get a SoC-internals answer out of the vendor, cite
ESP-IDF source directly. The time-multiplexing approach stands on both
ESP-IDF docs (`spi_bus_free`, SDSPI host init) and vendor confirmation.

## 4. See Also

- [`07-complete-gpio-and-io-map.md`](07-complete-gpio-and-io-map.md) — full GPIO map and pin conflicts table
- [`docs/waveshare-support/`](docs/waveshare-support/) — vendor support correspondence archive
- `CLAUDE.md` §"Critical Rules" — one-line summary of the SD/display sharing rule
- ESP-IDF `spi_master` and `sdspi_host` driver docs
