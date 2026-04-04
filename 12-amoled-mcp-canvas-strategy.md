# AMOLED MCP Canvas Strategy

Adapting the LCD-1.47 MCP canvas architecture (see [11-mcp-canvas-reference-architecture.md](11-mcp-canvas-reference-architecture.md)) to the ESP32-C6-Touch-AMOLED-1.8 board.

## The Core Problem

The LCD-1.47 project allocates a full-screen `lv_canvas` pixel buffer in BSS:

```
LCD-1.47:  172 × 320 × 2 bytes = 110,080 bytes (110 KB) ← fits easily
AMOLED:    368 × 448 × 2 bytes = 329,728 bytes (322 KB) ← does NOT fit
```

ESP32-C6 has ~512 KB SRAM total. After Wi-Fi (~55 KB), LVGL draw buffers, LVGL heap, FreeRTOS tasks, and other overhead, there's roughly **170 KB free** — not enough for a 322 KB canvas.

## Key Architectural Insight: The Display Has Its Own Framebuffer

**The SH8601 AMOLED controller has built-in full-screen GRAM (Graphics Display Data RAM).** This changes the problem fundamentally.

### How it works:

```
MCU (ESP32-C6)                          SH8601 Display Controller
┌──────────────────┐                    ┌──────────────────────────┐
│ LVGL widget tree │                    │ GRAM (full framebuffer)  │
│    (source of    │  QSPI DMA         │  368×448×2 = 322 KB     │
│     truth)       │ ──────────────►   │  (on-chip, not MCU RAM) │
│                  │  partial updates   │                          │
│ Draw buffers     │  (dirty regions    │ Panel refresh circuit    │
│ (1/10 screen)    │   only)            │ ──────────────────────► │ AMOLED
└──────────────────┘                    └──────────────────────────┘
```

- The **display controller holds the complete pixel state** and continuously refreshes the panel from its own GRAM — independently of the MCU
- The MCU **only sends changed pixels** to a windowed region of GRAM (via CASET 0x2A + RASET 0x2B + RAMWR 0x2C)
- LVGL's **partial rendering mode** is designed exactly for this: render dirty regions into small draw buffers, flush each chunk via DMA to the display's GRAM
- **The MCU does not need a framebuffer.** The display IS the framebuffer.
- **Reading back from GRAM** (register 0x2E RAMRD) is theoretically possible but practically never done in production — too slow over QSPI and unreliable

This is the standard architecture used by all production ESP32 + AMOLED projects: Waveshare official examples, LilyGo AMOLED series, Espressif BSP boards.

**Source:** [Espressif LCD Architecture Guide](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/lcd/lcd_guide.html) — "For SPI/QSPI LCDs with GRAM, the host processor only writes data to the GRAM, then the LCD driver IC reads GRAM data to drive the panel."

## Options Evaluated

### Option A: Half-Resolution Canvas (184×224) + LVGL 2× Zoom

Allocate a 184×224 canvas in BSS and display it at 2× zoom via `lv_img_set_zoom(canvas, 512)`.

```
Canvas: 184 × 224 × 2 = 82,432 bytes (80 KB)
```

MCP tools operate on a 184×224 coordinate space. Each logical pixel maps to a 2×2 block on the physical 368×448 display.

| Pros | Cons |
|------|------|
| Same architecture as ref project | Wastes AMOLED resolution (half in each dimension) |
| All 8 MCP tools work identically | 184×224 = 41K pixels (less than ref's 55K) |
| Snapshot pipeline unchanged | Coordinate space doesn't match physical display |
| BSS-allocated, zero fragmentation | Not how production projects handle this |

### Option B: Widget-Based Drawing (No Canvas Buffer) — RECOMMENDED

Use LVGL widgets at full 368×448 resolution. This is how LVGL is designed to work with GRAM-based displays.

| MCP Tool | LVGL Widget | Heap per object |
|----------|------------|-----------------|
| `draw_rect` | `lv_obj_t` + background/border style | ~100-200 B |
| `draw_text` | `lv_label_t` | ~150-300 B |
| `draw_line` | `lv_line_t` | ~120-200 B |
| `draw_arc` | `lv_arc_t` | ~200-300 B |
| `draw_path` | `lv_line_t` (polyline) or `lv_obj_t` (polygon via clip) | ~120-300 B |
| `clear_canvas` | `lv_obj_clean(screen)` — deletes all children | frees all |
| `get_canvas_info` | Query LVGL object tree | 0 B |
| `get_canvas_snapshot` | Flush-callback capture (see below) | 0 B extra |

| Pros | Cons |
|------|------|
| **Full 368×448 resolution** | Different render architecture from ref |
| **Industry-standard approach** for GRAM displays | `ui_display.c` rewrite (other files port directly) |
| LVGL handles dirty-region optimization | No arbitrary pixel-level drawing |
| Touch interaction works with real widgets | |
| Zero pixel buffer in MCU RAM | |
| Aligned with how Espressif/LVGL intend the stack to work | |

### Option C: Reduced Color Depth / Option D: Tiled Canvas

Both evaluated and rejected — too complex, too fragile, non-standard. See git history for details.

## Recommendation: Option B — Widget-Based, Full Resolution

### Addressing the Three Risks

#### Risk 1: OOM (LVGL heap exhaustion)

**Mitigation: object count cap + generous heap.**

- With 128 KB LVGL heap and ~200 bytes per widget, the theoretical max is ~640 objects
- **Hard cap at 128 objects.** `draw_*` returns MCP error `"Canvas full (128/128) — call clear_canvas"` beyond that
- `clear_canvas` calls `lv_obj_clean(screen)` — deletes all children, LVGL heap fully recovered
- `get_canvas_info` reports `objects: 12/128` — AI client can self-manage
- Typical MCP scene uses 10-30 objects — 128 cap is 4-12× headroom
- LVGL's memory pool allocator handles same-size alloc/free cycles well (minimal fragmentation)

#### Risk 2: Snapshot without a pixel buffer

**Mitigation: flush-callback interception (0 extra RAM for pixels).**

This is the production technique used by ESP32 LVGL screenshot projects. The idea: LVGL already renders every pixel through the flush callback — we just capture them during a forced full-screen refresh.

```
Normal operation:
  flush_cb(area, pixels) → esp_lcd_panel_draw_bitmap() → QSPI DMA → SH8601 GRAM

Snapshot capture:
  1. Set s_snap_active = true
  2. lv_obj_invalidate(screen)     ← force full-screen dirty
  3. lv_refr_now(NULL)             ← synchronous redraw in LVGL task
  4. flush_cb sees s_snap_active:
     a. Send to display (normal)
     b. ALSO downsample this strip into s_snap_rgb888[]
  5. After last strip: JPEG encode s_snap_rgb888[] → base64
  6. s_snap_active = false
```

**Memory cost:**
```
s_snap_rgb888[]:  92 × 112 × 3 = 30,912 B  (30 KB BSS) — 4× downsampled
s_jpeg_out[]:                     20,480 B  (20 KB BSS) — JPEG output
```

The downsample ratio (4×) matches the ref project's approach (172→86 = 2×, but our 368→92 = 4×). The output JPEG resolution (92×112) is close to the ref's (86×160) — adequate for MCP verification.

**Why this works reliably:**
- `lv_refr_now()` is synchronous — called from LVGL task, completes before returning
- LVGL processes all dirty areas in order, calling flush_cb for each strip
- Each flush_cb invocation receives a contiguous strip with known (x1,y1)-(x2,y2) coordinates
- We downsample each strip incrementally — no need to hold the full screen in memory
- The draw buffer (already allocated for normal operation) is the pixel source

**Reference:** [ESPHome LVGL screenshot](https://github.com/dcgrove/esphome-lvgl-screenshot), [Harald Kreuzer: LVGL screenshots via HTTP on ESP32](https://www.haraldkreuzer.net/en/news/create-lvgl-screenshots-esp32-and-send-them-http)

#### Risk 3: Scope of rewrite

**Scoped to one file — `ui_display.c`.**

| File | LCD-1.47 → AMOLED Change |
|------|--------------------------|
| `drawing_engine.c/.h` | Update `SCREEN_W=368, SCREEN_H=448`. Same queue, same types. |
| `mcp_server.c` | Update coordinate ranges in tool descriptions and `get_int()` calls. Same protocol. |
| `snapshot.c` | Replace canvas-read with flush-callback capture. New implementation, same API. |
| `main.c` | Add I2C → TCA9554 → AXP2101 init before display. Add touch init. |
| **`ui_display.c`** | **Rewrite:** `lv_canvas_draw_*` → widget creation. Same switch/case structure, different LVGL calls. |
| `amoled_driver/` (new) | New shared component for QSPI display, touch, IO expander. |

The MCP protocol layer, command queue, and build system are unchanged.

### RAM Budget

```
LVGL draw buf ×2 (BSS):    65,536 B  (64 KB) — 368×45×2 × 2 bufs, 1/10 screen
LVGL heap pool:            131,072 B  (128 KB) — widgets, styles, misc
Wi-Fi STA:                 ~55,000 B
FreeRTOS tasks:            ~25,000 B  (lvgl 16KB + httpd 8KB)
Snapshot RGB888 (BSS):      30,912 B  (30 KB) — 92×112×3
Snapshot JPEG (BSS):        20,480 B  (20 KB)
Snapshot HTTP (BSS):        20,480 B  (20 KB)
HTTP recv buffer:            2,048 B
Touch driver:               ~1,000 B
Misc heap (cJSON, etc.):  ~10,000 B
────────────────────────────────────────────────
Total:                     ~362 KB of 512 KB
Headroom:                  ~150 KB
```

Compared to LCD-1.47 (331 KB used / 181 KB headroom) — slightly more usage but comfortable margin. The 150 KB headroom supports future additions (audio, IMU, RTC) without RAM pressure.

## Snapshot: Flush-Callback Capture Detail

### Downsample Strategy

Display: 368×448. Snapshot target: 92×112 (4× reduction in each dimension).

In the flush callback, each strip covers some rows of the display. For each 4×4 block of source pixels within the strip, pick one sample (top-left) and write the RGB888 conversion to the snapshot buffer:

```c
// In flush callback, when s_snap_active:
for (int y = area->y1; y <= area->y2; y++) {
    if (y % 4 != 0) continue;  // only sample every 4th row
    int snap_y = y / 4;
    for (int x = area->x1; x <= area->x2; x++) {
        if (x % 4 != 0) continue;  // only sample every 4th column
        int snap_x = x / 4;
        int src_idx = (y - area->y1) * (area->x2 - area->x1 + 1) + (x - area->x1);
        lv_color_t px = color_map[src_idx];
        uint8_t *dst = &s_snap_rgb888[(snap_y * 92 + snap_x) * 3];
        dst[0] = (px.ch.red << 3) | (px.ch.red >> 2);
        dst[1] = (px.ch.green << 2) | (px.ch.green >> 4);
        dst[2] = (px.ch.blue << 3) | (px.ch.blue >> 2);
    }
}
```

### Synchronization

The snapshot must be triggered from the LVGL task (same task that calls `lv_timer_handler()`). The MCP HTTP handler runs in a different task. Pattern:

```
HTTP task (get_canvas_snapshot request):
  1. Set s_snap_requested = true
  2. xSemaphoreTake(s_snap_done_sem, timeout)  ← blocks until done

LVGL task (render timer, 50 ms):
  if (s_snap_requested) {
      s_snap_active = true;
      s_snap_requested = false;
      lv_obj_invalidate(lv_scr_act());
      lv_refr_now(NULL);           ← synchronous: all flush_cb calls happen here
      s_snap_active = false;
      jpeg_encode(s_snap_rgb888);  ← encode in LVGL task context
      xSemaphoreGive(s_snap_done_sem);
  }

HTTP task (resumes):
  3. Read JPEG result, base64 encode, return MCP response
```

## What Changes vs. LCD-1.47 Ref

### Unchanged
- `drawing_engine.h/.c` — queue pattern, command types, push functions (update SCREEN_W/H)
- `mcp_server.c` — JSON-RPC router, parameter validation, tool handlers (update coordinate ranges)
- `main.c` — startup sequence structure (add peripheral init steps)

### Changed

| Component | LCD-1.47 | AMOLED-1.8 |
|-----------|----------|------------|
| Display init | `LCD_Init()` — SPI2 + ST7789 | I2C + TCA9554 + QSPI + SH8601 |
| Backlight | `BK_Light(80)` — LEDC PWM GPIO 22 | SH8601 register 0x51 (0-255) |
| LVGL driver | SPI flush, 172×20 buffers | QSPI flush, 368×45 buffers, rounder callback |
| Drawing approach | `lv_canvas_draw_*` to pixel buffer | LVGL widgets (`lv_obj`, `lv_label`, `lv_line`, `lv_arc`) |
| Snapshot | Read from canvas buffer, 2× downsample | Flush-callback capture, 4× downsample |
| Coordinate ranges | x: 0-171, y: 0-319 | x: 0-367, y: 0-447 |
| Tool descriptions | "172×320 canvas" | "368×448 AMOLED display" |
| SCREEN_W/H | 172, 320 | 368, 448 |
| Partition | 2 MB app / 8 MB flash | 3 MB app / 16 MB flash |
| sdkconfig | 8 MB flash, 32 KB LVGL heap | 16 MB flash, 128 KB LVGL heap, touch |

### New

| Component | Description |
|-----------|-------------|
| TCA9554 init | Power on display + touch via IO expander before anything else |
| AXP2101 init | Configure power rails (optional but recommended) |
| Touch input | FT3168 I2C → LVGL `lv_indev_drv_t` pointer device |
| Rounder callback | SH8601 requires even-aligned coordinates |
| `amoled_driver/` | New shared component replacing `lcd_driver/` |

## Project File Structure

```
projects/11_mcp_canvas/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults.template
├── main/
│   ├── CMakeLists.txt
│   ├── main.c              # Startup: I2C → TCA9554 → display → LVGL → WiFi → MCP
│   ├── mcp_server.c/.h     # JSON-RPC + WiFi (adapted coordinate ranges)
│   ├── drawing_engine.c/.h # Command queue (same pattern, SCREEN_W=368, SCREEN_H=448)
│   ├── ui_display.c/.h     # Widget-based render + flush-capture snapshot
│   └── snapshot.c/.h       # Flush-callback capture + JPEG encode
```

Shared component:
```
shared/components/
├── amoled_driver/
│   ├── CMakeLists.txt
│   ├── idf_component.yml   # Dependencies: esp_lcd_sh8601, esp_lcd_touch_ft5x06, esp_io_expander_tca9554
│   ├── amoled.h/.c         # I2C bus, TCA9554, SH8601 QSPI init, brightness
│   ├── lvgl_driver.h/.c    # LVGL display driver, flush, rounder, tick timer
│   └── touch.h/.c          # FT3168 I2C + LVGL indev registration
└── lvgl__lvgl/             # LVGL 8.3.x
```
