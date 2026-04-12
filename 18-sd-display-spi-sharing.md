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
  I/O, tear SDSPI down, then reinitialize the display bus. The
  `amoled_release_spi()` / `amoled_reclaim_spi()` helper pattern in this repo
  exists for exactly this.
- **Boot-time batch access is simpler.** If SD access is only needed to load
  assets once, do it *before* `amoled_init()` and never switch again.
- **Display will be frozen** (no refreshes, no partial updates) for the entire
  duration that the SD card holds the bus. Plan UI state accordingly — e.g.
  show a static "Loading…" frame that was pushed before the handoff.

## 3. Waveshare Support Exchange

We contacted Waveshare (ticket **#235269**, replying engineer **赵玉娇 / Zhao
Yujiao**, 2026-04-05 → 2026-04-10) to confirm the intended usage pattern. The
thread did not produce a clear answer — their replies addressed the
pin/chip-select level, not the SPI peripheral host level we asked about.

Full verbatim transcript and assessment:
[`docs/waveshare-support/235269-sd-display-spi-sharing.md`](docs/waveshare-support/235269-sd-display-spi-sharing.md).

**Short version:** support claimed "QSPI and SPI are different, CS selects the
device, no conflict." That is wrong for ESP32-C6, where QSPI and 1-bit SPI are
modes of the same SPI2 controller. The time-multiplexing approach stands on
ESP-IDF docs for `spi_bus_free` and SDSPI host init, not on vendor
confirmation. For future C6 SPI peripheral questions, go straight to
ESP-IDF docs and the esp-idf GitHub issue tracker.

## 4. See Also

- [`07-complete-gpio-and-io-map.md`](07-complete-gpio-and-io-map.md) — full GPIO map and pin conflicts table
- [`docs/waveshare-support/`](docs/waveshare-support/) — vendor support correspondence archive
- `CLAUDE.md` §"Critical Rules" — one-line summary of the SD/display sharing rule
- ESP-IDF `spi_master` and `sdspi_host` driver docs
