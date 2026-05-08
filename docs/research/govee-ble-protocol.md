# Govee BLE Temperature/Humidity Protocol & Connection Approach

Reference for an upcoming ESP32-C6 project (Waveshare ESP32-C6-Touch-AMOLED-1.8) that
passively scans 3 Govee BLE temp/humidity sensors at home and renders them on the
1.8" AMOLED via LVGL.

Sources are listed at the bottom. Parsing formulas are quoted verbatim from the
Home Assistant `govee-ble` Python parser (the de-facto canonical implementation)
and cross-checked against `theengs/decoder` (C++) and `mkjanke/ESP32-NOW-Govee`
(Arduino C++ on real ESP32 hardware).

---

## 1. TL;DR — Connection Approach

| Decision | Choice |
|---|---|
| BLE stack | **NimBLE** (observer-only role) |
| Pairing / GATT | **No** — passive advertisement scan only |
| Encryption | None — Govee thermo-hygrometers broadcast plaintext |
| Cloud / WiFi required? | No |
| Filter target devices by | **Public MAC** (Govee uses fixed MAC, no rotation) |
| Reference parser to port | `Bluetooth-Devices/govee-ble/parser.py` → C, ~150 LOC |
| Working ESP32 reference | `mkjanke/ESP32-NOW-Govee` (NimBLE-Arduino, H5074) |

Passive scanning works for **every** H5xxx temperature/humidity model. None of
them require pairing, none encrypt their advertisements, none require the Govee
app or cloud for local readings. The cloud is only needed for graphs/alerts
inside Govee's phone app.

---

## 2. Govee Model Lineup (BLE Temp/Humidity)

Two BLE generations exist, distinguished by the manufacturer-data company ID
and local-name prefix.

| Model | Local name prefix | Mfg ID | Mfg data length | Parser family |
|---|---|---|---|---|
| H5051 | `Govee_H5051_xxxx` | 0xEC88 | 9 | `<hHB` LE @ d[1..5] |
| H5052 | `Govee_H5052_xxxx` | 0xEC88 | 9 | `<hHB` LE @ d[1..5] |
| H5071 | `Govee_H5071_xxxx` | 0xEC88 | 9 | `<hHB` LE @ d[1..5] |
| H5072 | `GVH5072_xxxx` | 0xEC88 | 6 | packed-int @ d[1..3] |
| **H5074** | `Govee_H5074_xxxx` | **0xEC88** | **7** | **`<hHB` LE @ d[1..5]** |
| **H5075** | `GVH5075_xxxx` | **0xEC88** | **6** | **packed-int @ d[1..3]** |
| H5100 | `GVH5100_xxxx` | 0x0001 | 6 | packed-int @ d[2..4] |
| H5101 | `GVH5101_xxxx` | 0x0001 | 6 | packed-int @ d[2..4] |
| H5102 | `GVH5102_xxxx` | 0x0001 | 6 | packed-int @ d[2..4] |
| H5103 | `GVH5103_xxxx` | 0x0001 | 6 | packed-int @ d[2..4] |
| H5104 | `GVH5104_xxxx` | 0x0001 | 6 | packed-int @ d[2..4] |
| H5105 | `GVH5105_xxxx` | 0x0001 | 6 | packed-int @ d[2..4] |
| H5108 | `GV5108_xxxx` | 0x0001 | 8 | packed-int (probe-only, no humidity) |
| H5174 | `GVH5174_xxxx` | 0x0001 | 6 | packed-int @ d[2..4] |
| H5177 | `GVH5177_xxxx` | 0x0001 | 6 | packed-int @ d[2..4] |
| H5178 | `H5178_xxxx`/`B5178_xxxx` | 0x0001 | 9 | multi-sensor (primary + remote) |
| H5179 (old fw) | `Govee_H5179_xxxx` | **0x8801** | 9 | `<hHB` LE @ d[4..8] |
| H5179 (new fw) | `GV5179_xxxx` | 0x0001 | 6 | parsed as H5100 family |
| H5181/82/83 | `GVH518x_xxxx` | varies | 14–17 | meat-probe thermometers (out of scope) |

Two important caveats:

1. The "0x0001" company ID is technically Ericsson Technology Licensing
   (a placeholder Govee co-opted on later firmwares). Disambiguate via the
   local-name prefix (`GVH5xxx` / `GV5xxx`).
2. Govee thermo-hygrometers advertise roughly every **1.0–3.5 s**. Empirically
   you'll see each device 5–20 times per minute. The H5074 sends two separate
   advertisements per cycle (one with name only, one with mfg data).

---

## 3. Advertising Packet Layouts

All snippets are quoted verbatim from
`https://github.com/Bluetooth-Devices/govee-ble/blob/main/src/govee_ble/parser.py`.

### 3.1 Shared helper (packed-int models)

```python
def decode_temp_humid(temp_humid_bytes: bytes) -> tuple[float, float]:
    """Decode potential negative temperatures."""
    base_num = (
        (temp_humid_bytes[0] << 16) + (temp_humid_bytes[1] << 8) + temp_humid_bytes[2]
    )
    is_negative = base_num & 0x800000
    temp_as_int = base_num & 0x7FFFFF
    temp_as_float = int(temp_as_int / 1000) / 10.0
    if is_negative:
        temp_as_float = -temp_as_float
    humid = (temp_as_int % 1000) / 10.0
    return temp_as_float, humid

def decode_temp_humid_battery_error(data: bytes) -> tuple[float, float, int, bool]:
    temp, humi = decode_temp_humid(data[0:3])
    batt = int(data[-1] & 0x7F)
    err  = bool(data[-1] & 0x80)   # high bit of battery byte = error flag
    return temp, humi, batt, err
```

The packed-int scheme: 3 bytes encode temperature + humidity together as a
24-bit value. The top bit is sign (NOT 2's-complement — mask with `0x7FFFFF`,
then negate). A 4th byte carries `(error << 7) | (battery & 0x7F)`.

### 3.2 H5072 / H5075 (mfg ID 0xEC88, 6 bytes)

```python
if msg_length == 6 and (
    "H5072" in local_name or "H5075" in local_name or mgr_id == 0xEC88
):
    temp, humi, batt, err = decode_temp_humid_battery_error(data[1:5])
```

Byte layout (after stripping the 2-byte company-ID prefix):

```
data[0]    = flag (always 0x00)
data[1..3] = packed temp+humid (24 bits, big-endian, top bit = sign)
data[4]    = (err_bit << 7) | battery_pct
data[5]    = trailing/padding
```

Worked example: `b"\x00\x03\x4D\xB2\x64\x00"` →
- `data[1:4] = 0x03 4D B2 = 216498`, top bit clear
- temp = `int(216498 / 1000) / 10.0` = **21.6 °C**
- humid = `(216498 % 1000) / 10.0` = **49.8 %**
- battery = `0x64 & 0x7F` = **100 %**

### 3.3 H5074 (mfg ID 0xEC88, 7 bytes) — simpler, no packed-int

```python
if msg_length == 7 and ("H5074" in local_name or mgr_id == 0xEC88):
    self.set_device_type("H5074")
    (temp, humi, batt) = PACKED_hHB_LITTLE.unpack(data[1:6])
    # temp / 100, humi / 100, batt as int
```

`PACKED_hHB_LITTLE = struct.Struct("<hHB")` — little-endian signed int16
temp, uint16 humid, uint8 battery.

```
data[0]    = 0x00 flag
data[1..2] = int16  LE, temp_C * 100
data[3..4] = uint16 LE, humidity_pct * 100
data[5]    = battery percent (0..100)
data[6]    = padding (often 0x02)
```

Worked example: `b"\x00\xE6\x09\xBC\x12\x64\x02"` →
- temp_raw = `0x09E6` LE = 2534 → **25.34 °C**
- humid_raw = `0x12BC` LE = 4796 → **47.96 %**
- battery = **100 %**

Cross-check from `mkjanke/ESP32-NOW-Govee` running on real hardware:
```cpp
double tempInC = ((double)((int16_t)((mfg_data[3] << 0) | (mfg_data[4]) << 8))) / 100;
double humPct  = ((double)((int16_t)((mfg_data[5] << 0) | (mfg_data[6]) << 8))) / 100;
uint8_t battPct = (uint8_t)mfg_data[7];
```
(Indices 3/4/5/6/7 because that code includes the 2-byte company-ID `0x88 0xEC`
in `mfg_data`. After stripping the company ID, indices become 1/2/3/4/5 — same
layout as HA's `data[1:6]`.)

### 3.4 H5100 family (mfg ID 0x0001, 6 or 8 bytes)

Includes H5100, H5101, H5102, H5103, H5104, H5105, H5174, H5177, and the
newer-firmware H5179 ("GV5179").

```python
if msg_length in (6, 8) and ("H5100" in local_name or "H5101" in local_name or ...):
    temp, humi, batt, err = decode_temp_humid_battery_error(data[2:6])
```

```
data[0..1] = 0x01 0x00  (flag bytes — note: same value as the 0x0001 mfg ID)
data[2..4] = packed temp+humid (24-bit, top bit = sign)
data[5]    = (err_bit << 7) | battery_pct
data[6..7] = (only for H5108) extra probe ID/padding
```

Worked example: `b"\x01\x01\x03\x46\x54\x64"` (H5100) →
- `data[2:5] = 0x03 46 54 = 214612`
- temp = 21.4 °C, humid = 61.2 %, battery = 100 %

### 3.5 H5179 old firmware (mfg ID 0x8801, 9 bytes)

```python
if msg_length == 9 and ("H5179" in local_name or mgr_id == 0x8801):
    self.set_device_type("H5179")
    temp, humi, batt = PACKED_hHB_LITTLE.unpack(data[4:9])
    # temp / 100, humi / 100, batt as int
```

```
data[0..3] = 0xEC 0x00 0x01 0x01  (Govee header)
data[4..5] = int16  LE, temp_C * 100
data[6..7] = uint16 LE, humid_pct * 100
data[8]    = battery percent
```

### 3.6 H5051 / H5052 / H5071 (mfg ID 0xEC88, 9 bytes)

```python
if msg_length == 9 and (mgr_id == 0xEC88 or "H5051" in local_name
                        or "H5052" in local_name or "H5071" in local_name):
    (temp, humi, batt) = PACKED_hHB_LITTLE.unpack(data[1:6])
    # temp / 100, humi / 100
```

Same `<hHB` LE layout as H5074, just with a longer trailing junk segment.

---

## 4. Parser Dispatch in C (drop-in for ESP-IDF)

```c
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int64_t  last_seen_us;
    int8_t   rssi;
    uint8_t  battery_pct;
    bool     valid;
    float    temp_c;
    float    humid_pct;
    char     model[8];
} govee_reading_t;

bool govee_decode(uint16_t company_id,
                  const uint8_t *d, uint8_t n,
                  const char *name,
                  govee_reading_t *out) {
    /* Strip Govee's "INTELLI_ROCKS" iBeacon trailer if present (25 bytes). */
    if (n > 25 && memmem(d, n, "INTELLI_ROCKS", 13)) n -= 25;

    /* Reject Apple iBeacon piggyback. */
    if (company_id == 0x004C) return false;

    /* H5074 — 0xEC88 + 7 bytes */
    if (company_id == 0xEC88 && n == 7) {
        int16_t  t = (int16_t)(d[1] | (d[2] << 8));
        uint16_t h = (uint16_t)(d[3] | (d[4] << 8));
        out->temp_c      = t / 100.0f;
        out->humid_pct   = h / 100.0f;
        out->battery_pct = d[5];
        strcpy(out->model, "H5074");
        goto sanity;
    }

    /* H5072 / H5075 — 0xEC88 + 6 bytes, packed-int @ d[1..3] */
    if (company_id == 0xEC88 && n == 6) {
        uint32_t base = ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
        bool neg = base & 0x800000;
        uint32_t v = base & 0x7FFFFF;
        out->temp_c      = (neg ? -1.0f : 1.0f) * ((int)(v / 1000) / 10.0f);
        out->humid_pct   = (v % 1000) / 10.0f;
        out->battery_pct = d[4] & 0x7F;
        if (d[4] & 0x80) return false;            /* error flag set */
        strcpy(out->model, "H5075");
        goto sanity;
    }

    /* H5100 family — 0x0001 + 6 or 8 bytes, packed-int @ d[2..4] */
    if (company_id == 0x0001 && (n == 6 || n == 8)) {
        uint32_t base = ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 8) | d[4];
        bool neg = base & 0x800000;
        uint32_t v = base & 0x7FFFFF;
        out->temp_c      = (neg ? -1.0f : 1.0f) * ((int)(v / 1000) / 10.0f);
        out->humid_pct   = (v % 1000) / 10.0f;
        out->battery_pct = d[5] & 0x7F;
        if (d[5] & 0x80) return false;
        strcpy(out->model, n == 6 ? "H5101" : "H5108");
        goto sanity;
    }

    /* H5179 old fw — 0x8801 + 9 bytes, hHB LE @ d[4..8] */
    if (company_id == 0x8801 && n == 9) {
        int16_t  t = (int16_t)(d[4] | (d[5] << 8));
        uint16_t h = (uint16_t)(d[6] | (d[7] << 8));
        out->temp_c      = t / 100.0f;
        out->humid_pct   = h / 100.0f;
        out->battery_pct = d[8];
        strcpy(out->model, "H5179");
        goto sanity;
    }

    /* H5051/52/71 — 0xEC88 + 9 bytes */
    if (company_id == 0xEC88 && n == 9) {
        int16_t  t = (int16_t)(d[1] | (d[2] << 8));
        uint16_t h = (uint16_t)(d[3] | (d[4] << 8));
        out->temp_c      = t / 100.0f;
        out->humid_pct   = h / 100.0f;
        out->battery_pct = d[5];
        strcpy(out->model, "H5051");
        goto sanity;
    }

    return false;

sanity:
    if (out->temp_c < -40.0f || out->temp_c > 100.0f ||
        out->humid_pct < 0.0f || out->humid_pct > 100.0f) return false;
    out->valid = true;
    return true;
}
```

---

## 5. ESP32-C6 BLE Stack & Scanner

### Why NimBLE

- C6 has no Bluetooth Classic — Bluedroid's main differentiator (BR/EDR) is unused.
- NimBLE: ~50 % less flash, ~100 KB less RAM than Bluedroid (Espressif numbers).
- IDF 5.5 BLE Get Started guides for C6 default to NimBLE.
- Observer-only footprint: ~47 KB IRAM static, ~14 KB DRAM static, ~30–40 KB heap.
  Comfortable in the project's ~168 KB headroom.

### menuconfig knobs

```
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_CONTROLLER_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_OBSERVER=y          # only need to receive advertisements
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n           # no GATT connections
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=n
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_SECURITY_ENABLE=n
CONFIG_BT_NIMBLE_LOG_LEVEL_INFO=y
```

### Minimal NimBLE scanner

```c
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

static int gap_event_cb(struct ble_gap_event *ev, void *arg) {
    if (ev->type != BLE_GAP_EVENT_DISC) return 0;

    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, ev->disc.data, ev->disc.length_data) != 0)
        return 0;
    if (!f.mfg_data || f.mfg_data_len < 6) return 0;

    uint16_t company_id = f.mfg_data[0] | (f.mfg_data[1] << 8);
    const uint8_t *payload = f.mfg_data + 2;
    uint8_t plen = f.mfg_data_len - 2;

    govee_reading_t r = { .rssi = ev->disc.rssi,
                          .last_seen_us = esp_timer_get_time() };
    if (govee_decode(company_id, payload, plen, (const char *)f.name, &r)) {
        // Match ev->disc.addr.val[0..5] against known[3] and queue update.
    }
    return 0;
}

void start_scan(void) {
    struct ble_gap_disc_params p = {
        .itvl              = BLE_GAP_LIM_DISC_SCAN_INT,
        .window            = BLE_GAP_LIM_DISC_SCAN_WINDOW,
        .filter_policy     = BLE_HCI_SCAN_FILT_NO_WL,
        .limited           = 0,
        .passive           = 1,    // Govee data is in primary ADV; no SCAN_REQ needed
        .filter_duplicates = 0,    // CRITICAL — duplicates are how we get new readings
    };
    ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &p, gap_event_cb, NULL);
}
```

### Filtering 3 known MACs

Don't use the controller whitelist; just `memcmp` inside the callback.
`ev->disc.addr.val[0]` is the MAC's LSB (little-endian).

```c
static const uint8_t known[3][6] = {
    {0xA4, 0x38, 0xC1, 0xA4, 0xDB, 0xF8},   // example — reverse byte order from sticker
    /* ... */
};
for (int i = 0; i < 3; i++)
    if (memcmp(ev->disc.addr.val, known[i], 6) == 0) { /* match */ break; }
```

Govee thermo-hygrometers use **fixed public MACs** — no rotation, so MAC
filtering is reliable indefinitely.

---

## 6. Architecture Sketch

```
app_main()
 ├── i2c_init() → AXP2101 → TCA9554 P4/P5 high → SH8601 → FT3168
 ├── lvgl_init() + display flush callback
 ├── ui_create()  — 3 sensor tiles (368×448 / 3 ≈ 368×149 each)
 └── nimble_port_init() + start_scan()

ble GAP callback (NimBLE host task):
 ├── Match MAC against known[3]
 ├── ble_hs_adv_parse_fields()
 ├── govee_decode()  → (temp_c, humid_pct, batt_pct, rssi, ts)
 └── xQueueOverwrite(sensor_q[i], &reading)

lv_timer (1 Hz):
 └── For each tile: xQueuePeek(sensor_q[i]); update labels
     (NEVER mutate LVGL outside lv_timer — see CLAUDE.md)
```

Three `xQueueCreate(1, sizeof(govee_reading_t))` slots, one per known device,
written via `xQueueOverwrite()` so the latest reading always wins. The LVGL
timer reads via `xQueuePeek()`.

---

## 7. Gotchas

1. **`INTELLI_ROCKS` trailer.** Many Govee firmwares append a 25-byte
   `\x02\x15INTELLI_ROCKS_HW...` block. HA strips it before length check
   (`if msg_length > 25 and b"INTELLI_ROCKS" in data: data = data[:-25]`).
   Without this, exact-length checks (== 6 / == 7) fail and packets are dropped.

2. **Apple iBeacon piggyback (company ID 76 / 0x004C).** Some Govee adverts
   carry a second mfg-data block with Apple's company ID. HA explicitly
   excludes it (`NOT_GOVEE_MANUFACTURER = {76}`).

3. **Two manufacturer data blocks per advertisement.** NimBLE's
   `ble_hs_adv_parse_fields()` only returns one. To get both, walk the TLVs
   manually (`length, type, payload`). In practice the Govee block usually
   appears first, so single-block parsing is good enough.

4. **H5051/52/71 vs H5074 disambiguation.** Both use 0xEC88; differ by length
   (9 vs 7). Branch on length, not just company ID.

5. **H5179 has two firmware variants.** Old: `Govee_H5179_xxxx` + 0x8801 + 9B.
   New: `GV5179_xxxx` + 0x0001 + 6B (parsed as H5100 family). A "H5179" device
   may take either path.

6. **Govee app pairing does not break advertising.** Common myth — H5xxx
   thermo-hygrometers keep broadcasting plaintext data forever. Unlike some
   Xiaomi sensors which encrypt after pairing.

7. **H5179 / H5102 "needs cloud" myth.** False. BLE advertisement is plaintext.
   Cloud is only for the Govee app's graphs/alerts.

8. **No MAC randomization** on Govee thermo-hygrometers — fixed public address.
   (The H512x button/motion family is encrypted with a time-based key —
   hard to decode locally, out of scope here.)

9. **Battery byte's high bit = error flag.** Mask with `& 0x7F` for percent.
   If high bit set, the temp/humid sample is invalid — discard.

10. **Don't trust the trailing padding byte.** H5074's 7th byte and H5075's
    6th byte vary across firmwares. Use only documented offsets.

11. **`filter_duplicates` is the #1 reason scanners "stop working".** Default
    examples set it ON. With Govee that means one reading per device per boot,
    then nothing. **Set to 0 for sensors.**

12. **Sign decode.** Packed-int format: high bit of the 24-bit value is sign,
    NOT 2's complement. Mask with `0x7FFFFF` to get magnitude, then negate.
    Naive sign-extension breaks readings near 0 °C.

13. **HA uses `int(temp_int / 1000) / 10.0` — integer division before
    negation.** Don't substitute `temp_int / 10000.0`; rounding diverges at
    sub-0.1 °C precision.

14. **C6 is single-core.** NimBLE host task at default prio 5; LVGL task lower.
    Use `xTaskCreate()` not `xTaskCreatePinnedToCore()` (per CLAUDE.md).

15. **WiFi+BLE coexistence.** ESP32-C6 shares the radio; with WiFi STA active,
    BLE scan loses ~10–30 % of packets. For pure scanner: leave WiFi off.

16. **Battery readings are coarse.** Govee often parks at 100 % for months
    then drops fast. Don't fire alerts on 99→95 transitions.

17. **Local name AD type.** Some firmwares only send Shortened Local Name
    (0x08), others Complete (0x09). NimBLE's parser handles both as
    `fields.name`; if walking TLVs manually, check both AD types.

18. **Company ID is little-endian on the wire.** Bytes `0x88 0xEC` = 0xEC88.
    Some blogs write "0x88EC" — same thing, byte-swapped. Use NimBLE's
    parser to avoid thinking about this.

---

## 8. Reference Repos

| Repo | Language | Why |
|---|---|---|
| [Bluetooth-Devices/govee-ble](https://github.com/Bluetooth-Devices/govee-ble) | Python | Canonical decoder powering Home Assistant. Single-file `parser.py`. Most up-to-date. |
| [theengs/decoder](https://github.com/theengs/decoder) | C++ (header-only DSL) | Pure C++ targeting embedded use. JSON-described decoders compile to a small lookup. |
| [wcbonner/GoveeBTTempLogger](https://github.com/wcbonner/GoveeBTTempLogger) | C++ (Linux BlueZ) | Long-running logger; broadest model coverage. |
| [mkjanke/ESP32-NOW-Govee](https://github.com/mkjanke/ESP32-NOW-Govee) | Arduino + NimBLE-Arduino | Working ESP32 H5074 reference; verbatim parsing code. |
| [Thrilleratplay/GoveeWatcher](https://github.com/Thrilleratplay/GoveeWatcher) | JS (noble) | Original H5075 reverse-engineering project. |
| [asednev/govee-bt-client](https://github.com/asednev/govee-bt-client) | TypeScript | Clean TS implementation. |
| [Home-Is-Where-You-Hang-Your-Hack/sensor.goveetemp_bt_hci](https://github.com/Home-Is-Where-You-Hang-Your-Hack/sensor.goveetemp_bt_hci) | Python | Older HA component, raw HCI parsing. |
| [Wim's H5075/H5074 RE writeup](https://wimsworld.wordpress.com/2020/07/11/govee-h5075-and-h5074-bluetooth-low-energy-and-mrtg/) | Blog | Byte-by-byte breakdown. |
| [theengs/gateway](https://github.com/theengs/gateway) | Python | BLE → MQTT bridge using Theengs decoder. |
| [ESPHome feature request #846](https://github.com/esphome/feature-requests/issues/846) | n/a | ESPHome has no native govee_ble component (still open). |

ESP-IDF references:
- [BLE Device Discovery (C6)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-guides/ble/get-started/ble-device-discovery.html)
- [NimBLE blecent example](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/nimble/blecent)
- [Bluedroid GATT client example](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/bluedroid/ble/gatt_client/main/gattc_demo.c)

---

## 9. Project Scope (locked)

- **Target devices:** 3× H5075 at home (parser §3.2 only — single branch).
- **Discovery:** auto, by local-name prefix `GVH5075_` + company ID `0xEC88`
  + mfg-data length 6. No hardcoded MACs. Show every H5075 the scan finds;
  pin the first 3 by RSSI or first-seen order, evict stale ones after a
  timeout (e.g. 60 s without an advert).
- **Output:** AMOLED display only. No SD logging. No WiFi/MQTT.
- **Power:** USB-powered (live monitor on a desk/wall), not battery-optimized.
