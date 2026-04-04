# 11_mcp_canvas — MCP Display Server

An [MCP (Model Context Protocol)](https://modelcontextprotocol.io/) server running on the ESP32-C6 that lets an AI agent draw to the 368x448 AMOLED display over WiFi.

## How It Works

The ESP32-C6 runs an HTTP server (JSON-RPC 2.0) on port 80. An AI client sends drawing commands via MCP `tools/call`, and the firmware creates LVGL widgets at full display resolution. The SH8601 AMOLED controller's built-in GRAM holds the pixel state — no MCU-side framebuffer needed.

```
AI Client (Claude / Python)
    │ HTTP POST /mcp
    ▼
ESP32-C6 HTTP Server ──► Drawing Queue ──► LVGL Widgets ──► SH8601 GRAM ──► AMOLED
    │                                                              ▲
    └── get_canvas_snapshot ──► flush-callback capture ──► JPEG ───┘
```

## MCP Tools (12)

| Tool | Description |
|------|-------------|
| `clear_canvas` | Fill display with solid color, remove all objects |
| `draw_rect` | Filled or outlined rectangle with optional corner radius |
| `draw_line` | Line segment with configurable width |
| `draw_arc` | Circular arc segment |
| `draw_text` | Text rendering (Montserrat 14px or 20px) |
| `draw_path` | Polyline (open) or polygon (closed) |
| `get_canvas_info` | Display dimensions, object count, queue depth |
| `get_canvas_snapshot` | Capture display as base64 JPEG (92x112, 4x downsampled) |
| `get_battery_info` | Battery voltage, %, charge state, USB status |
| `set_brightness` | AMOLED brightness 0-255 |
| `get_system_info` | Free heap, uptime, WiFi RSSI, IP |
| `power_off` | Software shutdown via AXP2101 PMIC |

## Hardware Used

| Peripheral | Role |
|-----------|------|
| SH8601 AMOLED | 368x448 display, QSPI 40MHz, built-in GRAM |
| FT3168 Touch | Capacitive touch input (I2C 0x38) |
| TCA9554 IO Expander | Powers display (P4) and touch (P5) |
| AXP2101 PMIC | Battery monitoring, long-press power off (2.5s) |
| ESP32-C6 WiFi | HTTP server, mDNS (esp32-canvas.local) |

## Architecture

- **Widget-based rendering** — each MCP draw command creates an LVGL widget at native 368x448 resolution. No pixel canvas buffer.
- **Display GRAM** — the SH8601 holds the full framebuffer on-chip. LVGL only sends dirty regions via QSPI DMA.
- **Flush-callback snapshot** — when `get_canvas_snapshot` is called, LVGL forces a full redraw and the flush callback downsamples each strip into a 92x112 RGB888 buffer, then JPEG-encodes it. Zero extra pixel RAM.
- **Object cap** — 128 max LVGL widgets. `clear_canvas` frees all. `get_canvas_info` reports count.
- **Async draw queue** — HTTP handler pushes to FreeRTOS queue (non-blocking), LVGL timer drains every 50ms.

## RAM Budget

```
LVGL draw buffers (DMA):  65 KB  (2x 368x45 RGB565)
LVGL heap pool:           64 KB  (widgets, styles)
WiFi stack:              ~55 KB
Snapshot buffers (BSS):   51 KB  (RGB888 + JPEG)
FreeRTOS tasks:          ~25 KB
Misc:                    ~15 KB
────────────────────────────────
Total:                  ~275 KB of 452 KB DRAM
Free at runtime:         ~55 KB
```

## Build & Flash

```bash
# Activate ESP-IDF
. ~/esp/esp-idf/export.sh

# One-time: copy WiFi credentials
cp sdkconfig.defaults.template sdkconfig.defaults
# Edit sdkconfig.defaults — set CONFIG_CANVAS_WIFI_SSID and CONFIG_CANVAS_WIFI_PASSWORD

# One-time: set target
idf.py set-target esp32c6

# Build & flash
idf.py build
idf.py -p /dev/cu.usbmodem3101 flash

# Monitor serial output
idf.py -p /dev/cu.usbmodem3101 monitor
```

## Endpoints

| Method | URI | Description |
|--------|-----|-------------|
| POST | `/mcp` | JSON-RPC 2.0 MCP endpoint |
| GET | `/ping` | Health check (JSON: status, objects, queue) |
| GET | `/snapshot.jpg` | Current display as JPEG image |

## Example: Draw a Rectangle

```bash
# Initialize
curl -X POST http://esp32-canvas.local/mcp \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize"}'

# Clear screen
curl -X POST http://esp32-canvas.local/mcp \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"clear_canvas","arguments":{"r":0,"g":0,"b":0}}}'

# Draw blue rectangle
curl -X POST http://esp32-canvas.local/mcp \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"draw_rect","arguments":{"x":50,"y":100,"w":268,"h":80,"r":30,"g":100,"b":255,"filled":true,"radius":12}}}'

# Verify with snapshot
curl http://esp32-canvas.local/snapshot.jpg -o canvas.jpg
```

## Power Management

- **Long press** power button (~2.5s) — hardware shutdown via AXP2101
- **Short press** — power on from shutdown
- **`power_off` MCP tool** — software shutdown, cuts all power rails
- **Battery monitoring** — `get_battery_info` returns voltage, %, charge state
