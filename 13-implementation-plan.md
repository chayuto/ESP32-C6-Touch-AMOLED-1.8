# Implementation Plan: MCP Canvas Server for AMOLED-1.8

## Goal

Build an MCP server on the ESP32-C6-Touch-AMOLED-1.8 that lets an AI client draw to the 368×448 AMOLED display over Wi-Fi using JSON-RPC 2.0. Port the proven architecture from the LCD-1.47 `11_mcp_server_display` project, adapted for widget-based rendering at full AMOLED resolution.

## Architecture

```
AI Client (Claude / Python)
    │ HTTP POST /mcp  (JSON-RPC 2.0)
    ▼
ESP32-C6  HTTP Server (port 80, esp32-canvas.local)
    │
    ├── mcp_server.c — JSON-RPC router, 8 tool handlers, Wi-Fi STA
    │       │
    │       ▼ xQueueSend (non-blocking)
    │
    ├── drawing_engine — FreeRTOS StaticQueue (depth 16)
    │       │
    │       ▼ Drained every 50 ms by LVGL render timer
    │
    ├── ui_display — Widget-based rendering (full 368×448)
    │       │         Each draw_* → creates LVGL widget
    │       │         clear_canvas → lv_obj_clean(screen)
    │       │         Object cap: 128 max
    │       ▼
    │
    ├── LVGL 8.x — Partial rendering (1/10 screen double buffer)
    │       │         Dirty region tracking
    │       │         Rounder callback (even alignment)
    │       ▼
    │
    ├── amoled_driver (shared component)
    │       ├── I2C bus (GPIO 7/8, 200kHz)
    │       ├── TCA9554 IO expander → P4=display, P5=touch power
    │       ├── SH8601 QSPI panel (40MHz, GPIO 0-5)
    │       ├── LVGL display driver (flush → esp_lcd_panel_draw_bitmap)
    │       └── FT3168 touch (I2C 0x38, INT GPIO 15) → LVGL indev
    │
    └── snapshot — Flush-callback capture
            Capture flag → lv_obj_invalidate → lv_refr_now
            Downsample 4× in flush_cb → 92×112 RGB888
            JPEG encode (esp_new_jpeg, quality 35)
            Base64 (mbedtls) → MCP response
```

## File Structure

```
shared/components/
├── amoled_driver/
│   ├── CMakeLists.txt          # Component registration
│   ├── idf_component.yml       # esp_lcd_sh8601, esp_lcd_touch_ft5x06, esp_io_expander_tca9554
│   ├── include/
│   │   ├── amoled.h            # Display + IO expander init API
│   │   ├── amoled_touch.h      # Touch init API
│   │   └── amoled_lvgl.h       # LVGL driver setup API
│   └── src/
│       ├── amoled.c            # I2C, TCA9554, SH8601 QSPI, brightness
│       ├── amoled_touch.c      # FT3168 I2C init
│       └── amoled_lvgl.c       # LVGL display driver, flush, rounder, tick, touch indev

projects/11_mcp_canvas/
├── CMakeLists.txt              # EXTRA_COMPONENT_DIRS + project()
├── partitions.csv              # 3MB app, 16MB flash
├── sdkconfig.defaults.template # ESP32-C6 config (no PSRAM, no BT)
├── Kconfig.projbuild           # Wi-Fi SSID/password menu
├── main/
│   ├── CMakeLists.txt          # Source files + REQUIRES
│   ├── idf_component.yml       # esp_new_jpeg, mdns dependencies
│   ├── main.c                  # Startup sequence
│   ├── mcp_server.h            # wifi_init_sta, mcp_server_start
│   ├── mcp_server.c            # JSON-RPC + Wi-Fi + HTTP + 8 tools
│   ├── drawing_engine.h        # Command types, queue, push API
│   ├── drawing_engine.c        # Static queue + push wrappers
│   ├── ui_display.h            # Widget render API
│   ├── ui_display.c            # Widget creation + render timer + snapshot capture
│   ├── snapshot.h              # Snapshot encode API
│   └── snapshot.c              # Flush-capture + JPEG + base64
```

## Shared Component: amoled_driver

### amoled.h / amoled.c — Hardware Init

**Responsibilities:**
1. I2C bus init (GPIO 7 SCL, GPIO 8 SDA, 200 kHz, I2C_NUM_0)
2. TCA9554 IO expander (0x20): P4/P5 LOW → 200ms → HIGH (power on display + touch)
3. SPI2 QSPI bus init (GPIO 0-4, DMA auto, 40 MHz)
4. SH8601 panel init (custom init commands, QSPI mode)
5. Brightness control via register 0x51

**API:**
```c
esp_lcd_panel_handle_t amoled_get_panel(void);
esp_err_t amoled_init(void);                    // Full init sequence
esp_err_t amoled_set_brightness(uint8_t level); // 0-255
```

**Init command table** (from Waveshare reference):
```c
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, {0x00}, 0, 120},           // Sleep Out + 120ms
    {0x44, {0x01, 0xD1}, 2, 0},       // Tear Scan Line
    {0x35, {0x00}, 1, 0},             // Tearing Effect On
    {0x53, {0x20}, 1, 10},            // CTRL Display
    {0x2A, {0x00,0x00,0x01,0x6F}, 4, 0}, // CASET 0-367
    {0x2B, {0x00,0x00,0x01,0xBF}, 4, 0}, // PASET 0-447
    {0x51, {0x00}, 1, 10},            // Brightness 0
    {0x29, {0x00}, 0, 10},            // Display On
    {0x51, {0xD0}, 1, 0},             // Brightness 208/255
};
```

### amoled_touch.h / amoled_touch.c — Touch Controller

**Responsibilities:**
1. FT3168 init via esp_lcd_touch_ft5x06 API (I2C bus already initialized by amoled.c)
2. Return `esp_lcd_touch_handle_t` for LVGL registration

**API:**
```c
esp_err_t amoled_touch_init(esp_lcd_touch_handle_t *out_touch);
```

### amoled_lvgl.h / amoled_lvgl.c — LVGL Integration

**Responsibilities:**
1. `lv_init()`, draw buffer allocation (heap_caps DMA), `lv_disp_draw_buf_init`
2. Display driver registration with flush_cb, rounder_cb
3. Tick timer (2ms via esp_timer)
4. Touch input device registration
5. **Flush callback hook** for snapshot capture — the key innovation

**API:**
```c
esp_err_t amoled_lvgl_init(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t touch);
lv_disp_t *amoled_lvgl_get_disp(void);

// Snapshot capture support
typedef void (*amoled_flush_hook_t)(const lv_area_t *area, lv_color_t *color_map);
void amoled_lvgl_set_flush_hook(amoled_flush_hook_t hook);
```

**Draw buffer sizing (1/10 screen, double buffered):**
```
368 × 45 × 2 bytes = 33,120 bytes per buffer
× 2 buffers = 66,240 bytes total
Allocated via heap_caps_malloc(MALLOC_CAP_DMA)
```

**Rounder callback** (from Waveshare reference, required by SH8601):
```c
void amoled_lvgl_rounder_cb(lv_disp_drv_t *drv, lv_area_t *area) {
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}
```

**Flush callback with hook:**
```c
void amoled_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2+1, area->y2+1, color_map);
    // Snapshot hook — captures pixels during forced refresh
    if (s_flush_hook) {
        s_flush_hook(area, color_map);
    }
}
```

## Project: 11_mcp_canvas

### main.c — Startup Sequence

```
1. NVS init
2. amoled_init()         — I2C, TCA9554, QSPI, SH8601
3. amoled_touch_init()   — FT3168
4. amoled_lvgl_init()    — LVGL + display driver + touch indev
5. drawing_engine_init() — static queue
6. ui_display_init()     — register render timer, set flush hook
7. ui_display_show_connecting()
8. xTaskCreate(lvgl_task) — 16KB stack, priority 2
9. wifi_init_sta()        — blocks up to 30s
10. ui_display_show_ip()
11. mcp_server_start()    — mDNS + HTTP
```

### drawing_engine.h / drawing_engine.c — Command Queue

Same pattern as LCD-1.47 ref. Changes:
- `SCREEN_W = 368`, `SCREEN_H = 448`
- `DRAW_QUEUE_LEN = 16` (doubled from 8 — larger display, more concurrent commands)
- `MAX_TEXT_LEN = 255` (increased — wider display fits more text)

### ui_display.h / ui_display.c — Widget-Based Rendering

The main architectural change from the ref project. Instead of `lv_canvas_draw_*()` to a pixel buffer, each command creates LVGL widgets.

**Object tracking:**
```c
#define MAX_OBJECTS 128
static int s_obj_count = 0;
static lv_obj_t *s_draw_screen = NULL;  // Screen for drawing objects
```

**Render timer (50ms) — drain queue, create widgets:**
```c
void ui_render_timer_cb(lv_timer_t *timer) {
    draw_cmd_t cmd;
    while (xQueueReceive(g_draw_queue, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
            case CMD_CLEAR:
                lv_obj_clean(s_draw_screen);  // Delete all children
                s_obj_count = 0;
                // Set bg color
                lv_obj_set_style_bg_color(s_draw_screen,
                    lv_color_make(cmd.clear_color.r, cmd.clear_color.g, cmd.clear_color.b), 0);
                break;

            case CMD_DRAW_RECT:
                if (s_obj_count >= MAX_OBJECTS) break;
                lv_obj_t *rect = lv_obj_create(s_draw_screen);
                lv_obj_remove_style_all(rect);
                lv_obj_set_pos(rect, cmd.rect.x, cmd.rect.y);
                lv_obj_set_size(rect, cmd.rect.w, cmd.rect.h);
                if (cmd.rect.filled) {
                    lv_obj_set_style_bg_color(rect, lv_color_make(...), 0);
                    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
                } else {
                    lv_obj_set_style_bg_opa(rect, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_border_color(rect, lv_color_make(...), 0);
                    lv_obj_set_style_border_width(rect, 1, 0);
                }
                lv_obj_set_style_radius(rect, cmd.rect.radius, 0);
                lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE);
                s_obj_count++;
                break;

            case CMD_DRAW_TEXT:
                if (s_obj_count >= MAX_OBJECTS) break;
                lv_obj_t *label = lv_label_create(s_draw_screen);
                lv_label_set_text(label, cmd.text.text);
                lv_obj_set_pos(label, cmd.text.x, cmd.text.y);
                lv_obj_set_style_text_color(label, lv_color_make(...), 0);
                lv_obj_set_style_text_font(label,
                    cmd.text.font_size == 20 ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
                s_obj_count++;
                break;

            case CMD_DRAW_LINE:
                if (s_obj_count >= MAX_OBJECTS) break;
                lv_obj_t *line = lv_line_create(s_draw_screen);
                static lv_point_t line_pts[2]; // reused per command
                line_pts[0] = (lv_point_t){cmd.line.x1, cmd.line.y1};
                line_pts[1] = (lv_point_t){cmd.line.x2, cmd.line.y2};
                lv_line_set_points(line, line_pts, 2);
                lv_obj_set_style_line_color(line, lv_color_make(...), 0);
                lv_obj_set_style_line_width(line, cmd.line.width, 0);
                s_obj_count++;
                break;

            case CMD_DRAW_ARC:
                if (s_obj_count >= MAX_OBJECTS) break;
                lv_obj_t *arc = lv_arc_create(s_draw_screen);
                // Configure arc...
                s_obj_count++;
                break;

            case CMD_DRAW_PATH:
                if (s_obj_count >= MAX_OBJECTS) break;
                lv_obj_t *path_line = lv_line_create(s_draw_screen);
                lv_line_set_points(path_line, cmd.path.pts, cmd.path.pt_cnt);
                // Configure style...
                s_obj_count++;
                break;
        }
    }
}
```

### snapshot.h / snapshot.c — Flush-Callback Capture

**Snapshot buffer (BSS):**
```c
#define SNAP_W  92   // 368 / 4
#define SNAP_H  112  // 448 / 4
static uint8_t s_snap_rgb888[SNAP_W * SNAP_H * 3];  // 30,912 bytes
static uint8_t s_snap_jpeg[20480];                    // 20 KB
```

**Capture flow:**
```
1. HTTP handler sets s_snap_requested = true
2. HTTP handler blocks on s_snap_done semaphore (timeout 2s)
3. Render timer sees s_snap_requested:
   a. Sets flush hook via amoled_lvgl_set_flush_hook(snap_flush_hook)
   b. lv_obj_invalidate(lv_scr_act())
   c. lv_refr_now(NULL)  — synchronous, all flush_cb calls happen here
   d. Clears flush hook
   e. JPEG encodes s_snap_rgb888
   f. Gives s_snap_done semaphore
4. HTTP handler wakes, reads JPEG, base64 encodes, returns MCP response
```

**Flush hook (called for each strip during forced refresh):**
```c
static void snap_flush_hook(const lv_area_t *area, lv_color_t *color_map) {
    int w = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; y++) {
        if (y % 4 != 0) continue;
        int snap_y = y / 4;
        if (snap_y >= SNAP_H) continue;
        for (int x = area->x1; x <= area->x2; x++) {
            if (x % 4 != 0) continue;
            int snap_x = x / 4;
            if (snap_x >= SNAP_W) continue;
            lv_color_t px = color_map[(y - area->y1) * w + (x - area->x1)];
            uint8_t *dst = &s_snap_rgb888[(snap_y * SNAP_W + snap_x) * 3];
            dst[0] = (px.ch.red << 3) | (px.ch.red >> 2);
            dst[1] = (px.ch.green << 2) | (px.ch.green >> 4);
            dst[2] = (px.ch.blue << 3) | (px.ch.blue >> 2);
        }
    }
}
```

### mcp_server.c — MCP Protocol

Same JSON-RPC 2.0 structure as LCD-1.47 ref. Key changes:

**Coordinate ranges updated:**
- x: 0-367, y: 0-447 (was 0-171, 0-319)
- radius max: 224 (was 160)
- Descriptions reference "368x448 AMOLED"

**Tool descriptions updated for widget-based approach:**
```c
"clear_canvas" → "Fill the 368x448 AMOLED display with a solid colour. ..."
"get_canvas_info" → Also reports object count: "Objects: 12/128 | ..."
"get_canvas_snapshot" → "Captures current display as JPEG (92x112, 4x downsampled) ..."
```

**New: object count in error responses:**
```c
if (s_obj_count >= MAX_OBJECTS) {
    send_error(req, id, -32603, "Canvas full (128/128) — call clear_canvas");
    return;
}
```

### Build Configuration

**partitions.csv:**
```csv
# Name,     Type, SubType, Offset,   Size,   Flags
nvs,        data, nvs,     ,         0x6000,
phy_init,   data, phy,     ,         0x1000,
factory,    app,  factory, ,         3M,
```

**sdkconfig.defaults.template:**
```ini
CONFIG_IDF_TARGET="esp32c6"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# LVGL
CONFIG_LV_MEM_SIZE_KILOBYTES=128
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_COLOR_16_SWAP=y

# Disable BT — save ~50KB flash + ~30KB RAM
CONFIG_BT_ENABLED=n

# FreeRTOS
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_UNICORE=y

# HTTP server
CONFIG_HTTPD_MAX_URI_LEN=512
CONFIG_HTTPD_MAX_REQ_HDR_LEN=512

# Watchdog
CONFIG_ESP_TASK_WDT_TIMEOUT_S=15

# Wi-Fi
CONFIG_CANVAS_WIFI_SSID="YOUR_SSID_HERE"
CONFIG_CANVAS_WIFI_PASSWORD="YOUR_PASSWORD_HERE"
```

## Debug Visibility

Every module logs at INFO level for init success, WARN for recoverable issues, ERROR for failures:

```
[main]     I: Starting MCP Canvas
[amoled]   I: I2C bus initialized (SCL=7, SDA=8, 200kHz)
[amoled]   I: TCA9554 found at 0x20, powering display+touch
[amoled]   I: SH8601 panel initialized (368x448, QSPI 40MHz)
[amoled]   I: Brightness set to 208
[touch]    I: FT3168 initialized (I2C 0x38, INT=GPIO15)
[lvgl_drv] I: LVGL initialized (draw bufs: 2x33120 DMA, tick: 2ms)
[lvgl_drv] I: Display driver registered (368x448, rounder ON)
[lvgl_drv] I: Touch input registered
[draw_eng] I: Draw queue ready (depth=16, cmd_size=XXX bytes)
[ui]       I: Render timer registered (50ms)
[ui]       I: Object cap: 128
[mcp]      I: Wi-Fi connecting to "SSID"...
[mcp]      I: Connected, IP: 192.168.1.100
[mcp]      I: Modem sleep disabled
[mcp]      I: mDNS: esp32-canvas.local
[mcp]      I: HTTP server: POST /mcp  GET /ping  GET /snapshot.jpg
[mcp]      I: Tools JSON cached (XXXX bytes)
[mcp]      I: MCP server ready
```

Runtime logging:
```
[mcp]      D: tools/call draw_rect {x:10,y:20,w:100,h:50}
[ui]       D: +rect obj_count=1/128
[snap]     I: Capture started (forced invalidation)
[snap]     I: JPEG encoded: 3847 bytes (92x112)
[snap]     I: Base64: 5132 bytes
[mcp]      W: Queue full — command dropped
[ui]       W: Object cap reached (128/128)
```

Heap monitoring at startup:
```
[main]     I: Free heap: XXXXX bytes (min: XXXXX)
[main]     I: Free DMA-capable: XXXXX bytes
```

## RAM Budget (Final)

```
LVGL draw buf ×2 (DMA heap):  66,240 B  (64 KB) — 368×45×2 × 2
LVGL heap pool:               131,072 B  (128 KB)
Wi-Fi STA:                    ~55,000 B
FreeRTOS tasks:               ~25,000 B  (lvgl 16KB + httpd 8KB)
Snapshot RGB888 (BSS):         30,912 B  (30 KB) — 92×112×3
Snapshot JPEG (BSS):           20,480 B  (20 KB)
Snapshot HTTP (BSS):           20,480 B  (20 KB)
HTTP recv buffer:               2,048 B
Draw queue (BSS):              ~4,096 B  (16 × ~256 bytes)
Touch driver:                  ~1,000 B
Misc heap (cJSON, mDNS):     ~15,000 B
────────────────────────────────────────────────
Total:                        ~371 KB of 512 KB
Headroom:                     ~141 KB
```

## Risk Mitigations

| Risk | Mitigation | Fallback |
|------|-----------|----------|
| SH8601 init fails | Extensive logging at each step; TCA9554 power cycle | Check I2C scan, verify TCA9554 P4/P5 |
| LVGL heap OOM | 128 object cap, clear_canvas frees all | Reduce cap to 64, increase LVGL heap |
| Snapshot timing | Synchronous lv_refr_now() in LVGL task context | Add timeout, return error |
| DMA buffer alloc fails | assert() on heap_caps_malloc | Reduce to 1/20 screen (halve buffer) |
| Wi-Fi won't connect | 30s timeout, halt with log | Board shows "Connecting..." on display |
| Touch not working | Non-critical for MCP; log warning, continue | MCP works without touch |

## Implementation Order

1. **amoled_driver component** — get display lit up first
2. **LVGL integration** — confirm widgets render on screen
3. **Drawing engine + ui_display** — widget-based rendering
4. **MCP server + WiFi** — port from ref, adjust coordinates
5. **Snapshot** — flush-callback capture
6. **Build + flash + test** — iterate until working
