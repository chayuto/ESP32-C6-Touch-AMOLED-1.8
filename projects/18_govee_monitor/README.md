# 18_govee_monitor

Live monitor for Govee H5075 BLE temperature/humidity sensors on the
Waveshare ESP32-C6-Touch-AMOLED-1.8.

The board passively scans for nearby H5075 advertisements (no pairing,
no cloud, no WiFi) and renders the latest reading from each on the AMOLED
as a vertical stack of tiles.

```
┌──────────────────────────────┐
│ Bedroom               -52 dBm│
│  21.6°C       49.8%          │
│ ▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌  100%   │
├──────────────────────────────┤
│ Kitchen               -64 dBm│
│  24.1°C       42.0%          │
│ ▌▌▌▌▌▌▌▌▌▌▌▌▌▌▌      78%    │
├──────────────────────────────┤
│ Searching...                 │
│         (spinner)            │
└──────────────────────────────┘
```

## Features

- **Pure BLE observer** (NimBLE, no GATT) — works with stock H5075 firmware.
- **Auto-discovery** — leave the configuration empty and the board fills slots
  with the first H5075s it sees, labelling them `H5075-XXXX` (last 4 hex of MAC).
- **Compile-time pinning** — populate `device_config.h` with `{MAC, label}`
  pairs to assign friendly names like "Bedroom" / "Kitchen" / "Outside".
  Pinned slots never rotate or evict.
- **RSSI rotation with hysteresis** — if more H5075s are in range than the
  display can show, the strongest win. New devices replace the weakest auto
  slot only if they beat it by ≥6 dB (prevents thrashing).
- **Stale + evict timeouts** — slots grey out after 60 s of silence and
  unpinned slots free up after 5 min so a new device can take their place.
- **°C / °F toggle** — Kconfig (`CONFIG_GOVEE_TEMP_UNIT_F`).
- **Type scales with slot count** — at 4 slots each tile is 112 px tall, so the
  temperature and humidity fonts step down a size to avoid colliding with the
  header and battery rows.

## Build / Flash

```zsh
. ~/esp/esp-idf/export.sh
idf.py -C projects/18_govee_monitor set-target esp32c6      # one-time
idf.py -C projects/18_govee_monitor build
idf.py -C projects/18_govee_monitor -p /dev/cu.usbmodem1101 flash
```

Or use the project skills:

```
/build 18_govee_monitor
/flash 18_govee_monitor
```

## Pinning specific sensors with friendly labels

Real MACs and labels live in `main/device_config.h`, which is **gitignored**
(same pattern as `sdkconfig.defaults` for WiFi creds). Copy the template on
first checkout:

```
cp projects/18_govee_monitor/main/device_config.h.template \
   projects/18_govee_monitor/main/device_config.h
```

Then edit `main/device_config.h`:

```c
static const govee_known_device_t GOVEE_KNOWN[] = {
    { {0xA4, 0xC1, 0x38, 0x12, 0x34, 0x56}, "Bedroom" },
    { {0xA4, 0xC1, 0x38, 0x78, 0x9A, 0xBC}, "Kitchen" },
    { {0xA4, 0xC1, 0x38, 0xDE, 0xF0, 0x12}, "Outside" },
};
```

The 6-byte MAC is in **big-endian byte order** — exactly as printed on the
sensor sticker (e.g. `A4:C1:38:12:34:56`). Each pinned entry occupies the
matching slot index (0, 1, 2…) and shows the configured label.

To find the MAC of a sensor, leave `GOVEE_KNOWN[]` empty, flash, and read
the BLE scanner log:

```
I (5234) ble: GVH5075_DBF8 A4:C1:38:DB:F8:00 T=21.6 H=49.8 bat=100 rssi=-52
```

Copy those MACs into `GOVEE_KNOWN[]` and rebuild.

## Configuration knobs (`idf.py menuconfig` → "Govee Monitor")

| Option | Default | Effect |
|---|---|---|
| `GOVEE_MAX_SLOTS` | 4 | Max sensors visible at once (1–4) |
| `GOVEE_STALE_TIMEOUT_S` | 60 | A slot greys out after this much silence |
| `GOVEE_EVICT_TIMEOUT_S` | 300 | Unpinned slot is freed for a new device |
| `GOVEE_TEMP_UNIT_F` | n | Show °F instead of °C |

## Source layout

```
main/
├── main.c               # boot order: AMOLED → LVGL → slot_store → BLE scanner
├── ble_scanner.c        # NimBLE observer, GAP callback, MAC reverse, log
├── govee_decoder.c      # H5075 packed-int parser (HA parity)
├── slot_store.c         # pinned/auto slots, RSSI EWMA, rotation, mutex
├── ui.c                 # N-tile vertical layout, refresh from snapshot
├── device_config.h.template  # template — copy to device_config.h (gitignored)
└── Kconfig.projbuild    # GOVEE_MAX_SLOTS, timeouts, °C/°F
```

## Protocol reference

Full BLE advertisement layout, parser derivations, gotchas, and reference
implementations are documented in
[`docs/research/govee-ble-protocol.md`](../../docs/research/govee-ble-protocol.md).

The decoder ports the H5075 branch of Home Assistant's
[`govee-ble parser.py`](https://github.com/Bluetooth-Devices/govee-ble/blob/main/src/govee_ble/parser.py)
to C, with cross-checks from
[`theengs/decoder`](https://github.com/theengs/decoder) and
[`mkjanke/ESP32-NOW-Govee`](https://github.com/mkjanke/ESP32-NOW-Govee).

## Adding more Govee model families

The protocol research doc covers H5074 (`<hHB` LE format), H5100 family
(packed-int @ d[2..4], company ID 0x0001), H5179 (old fw 0x8801, new fw 0x0001),
and the H5051/52/71 legacy variants. Drop additional branches into
`govee_decoder.c` and adjust the filter in `ble_scanner.c::gap_event_cb`.
