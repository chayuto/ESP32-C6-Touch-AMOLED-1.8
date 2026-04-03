# ESP32-C6 AMOLED Display UI Reference

Reference for LVGL 8.x UI work on the Waveshare ESP32-C6-Touch-AMOLED-1.8.
Invoke when designing, debugging, or modifying any display/UI code.

---

## Screen Geometry

```
Width  : 368 px  (wider than LCD-1.47 — fits ~40 chars at font_montserrat_14)
Height : 448 px  (tall portrait — great for lists, dashboards, watch faces)
Origin : top-left (0,0)
Colour : 16-bit RGB565 via SH8601 QSPI AMOLED
Type   : AMOLED — true black, infinite contrast, self-emitting
```

### Coordinate Constraint (Critical)

**All draw coordinates must be divisible by 2.** The SH8601 QSPI protocol requires even alignment.
LVGL must use a rounder callback:

```c
static void lvgl_rounder_cb(lv_disp_drv_t *drv, lv_area_t *area) {
    area->x1 = area->x1 & ~1;  // round down to even
    area->y1 = area->y1 & ~1;
    area->x2 = area->x2 | 1;   // round up to odd (makes width even)
    area->y2 = area->y2 | 1;
}
// Register: disp_drv.rounder_cb = lvgl_rounder_cb;
```

### Usable layout budget (typical header + body pattern)

| Zone         | Y start | Height  | Notes                        |
|---|---|---|---|
| Title label  | 8       | ~24 px  | font_montserrat_20 bold      |
| Status label | 36      | ~20 px  | font_montserrat_14 normal    |
| Body / table | 60      | 388 px  | 448 - 60 = 388 px remaining |

The larger 388px body area fits significantly more content than the LCD-1.47's 268px.

---

## Font Sizes

Enable in `sdkconfig.defaults` (or `lv_conf.h` for Arduino):

| Symbol                    | Height | Best for                    | Chars/line (368px) |
|---|---|---|---|
| `&lv_font_montserrat_10`  | 10 px  | tiny labels, units          | ~52 |
| `&lv_font_montserrat_14`  | 14 px  | **default** — table cells   | ~40 |
| `&lv_font_montserrat_18`  | 18 px  | section headers             | ~32 |
| `&lv_font_montserrat_20`  | 20 px  | titles, values              | ~28 |
| `&lv_font_montserrat_24`  | 24 px  | large values, clock digits  | ~22 |
| `&lv_font_montserrat_36`  | 36 px  | hero numbers, watch face    | ~14 |

---

## lv_table Row Height Calculation

```
row_height = font_height + pad_top + pad_bottom
           = 14          + 8       + 8           = 30 px  (LVGL default)
           = 14          + 3       + 3           = 20 px  (compact)
```

**For 388px body height:**
- Default 30 px/row → 12 data rows + header = 13 rows max
- Compact 20 px/row → 19 data rows + header = 20 rows max

Compact padding for dense tables:
```c
lv_obj_set_style_pad_top(table,    3, LV_PART_ITEMS);
lv_obj_set_style_pad_bottom(table, 3, LV_PART_ITEMS);
lv_obj_set_style_pad_left(table,   4, LV_PART_ITEMS);
lv_obj_set_style_pad_right(table,  4, LV_PART_ITEMS);
lv_obj_set_height(table, 388);
```

---

## Column Width Budget (368 px total)

More than 2× the LCD-1.47's 172px — allows richer layouts.

| Layout                    | Col widths              | Sum |
|---|---|---|
| Label + Value (2-col)     | 140 + 228 = 368        | 368 |
| Icon + Name + Value       | 40 + 200 + 128 = 368   | 368 |
| GPIO + Mode + Value       | 60 + 170 + 138 = 368   | 368 |
| 4-column dense            | 60 + 100 + 100 + 108   | 368 |

Characters per column at font_montserrat_14 (~8 px/char):
- 60 px → ~7 chars
- 140 px → ~17 chars
- 200 px → ~25 chars

---

## Color Rules

**AMOLED means true black pixels are OFF (zero power).** Use black backgrounds aggressively.

```c
// Screen background — true black (AMOLED pixels off)
lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);

// Text — white or light colours for contrast
lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);

// Table cells — must set LV_PART_ITEMS, not LV_PART_MAIN
lv_obj_set_style_text_color(table, lv_color_white(), LV_PART_ITEMS);
```

**AMOLED colour palette** (vibrant on AMOLED, readable):

| State         | lv_color_make(r, g, b) | Appearance     |
|---|---|---|
| Accent cyan   | (0, 200, 255)          | bright cyan    |
| Accent green  | (0, 220, 80)           | bright green   |
| Accent amber  | (255, 180, 0)          | warm amber     |
| Accent red    | (255, 60, 60)          | bright red     |
| Accent purple | (180, 80, 255)         | vivid purple   |
| Muted text    | (120, 120, 120)        | grey           |
| Header row    | (30, 30, 50)           | near-black     |
| Card bg       | (20, 20, 25)           | very dark      |

**Use `lv_color_make(r,g,b)` — NOT `LV_COLOR_MAKE(R,G,B)`** (uppercase macro = compile error).

---

## Touch Input Integration

This board has capacitive touch — all LVGL interactive widgets work natively.
Register touch as LVGL input device during init:

```c
static lv_indev_drv_t indev_drv;
lv_indev_drv_init(&indev_drv);
indev_drv.type = LV_INDEV_TYPE_POINTER;
indev_drv.read_cb = touch_read_cb;
indev_drv.user_data = touch_handle;
lv_indev_drv_register(&indev_drv);
```

### Touch-friendly UI sizing
- **Minimum touch target:** 48×48 px (recommended 60×60 for reliable finger taps)
- **Button padding:** At least 10px around text
- **List item height:** Minimum 48px for comfortable scrolling
- **Gesture areas:** Leave edge zones (20px) clear for swipe gestures

### FT3168 Gestures (available via register 0xD3)
| ID | Gesture | Use case |
|---|---|---|
| 0x20 | Swipe Left | Next page/screen |
| 0x21 | Swipe Right | Previous page/screen |
| 0x22 | Swipe Up | Scroll down |
| 0x23 | Swipe Down | Scroll down / pull-to-refresh |
| 0x24 | Double Click | Toggle/select |

---

## Brightness Control (AMOLED-specific)

No backlight PWM — brightness is set via SH8601 display register:

```c
// Arduino_GFX:
gfx->setBrightness(200);  // 0-255

// ESP-IDF (via esp_lcd panel handle):
uint8_t brightness = 200;
esp_lcd_panel_io_tx_param(io_handle, 0x51, &brightness, 1);
```

For power-saving: reduce brightness in idle, set to 0 before deep sleep.

---

## AMOLED-Specific Design Tips

1. **True black is free** — AMOLED pixels that display black consume zero power.
   Design dark UIs with black backgrounds to extend battery life.
2. **Burn-in awareness** — Static bright elements can cause OLED burn-in over time.
   Consider screen timeout, dimming, or subtle animations for always-on elements.
3. **High contrast ratio** — 100,000:1 means fine details pop. Use thin borders
   and subtle color differences that would be invisible on LCD.
4. **Wide viewing angle** — 178° means no color shift. Don't worry about off-axis readability.
5. **AOD (Always-On Display)** — SH8601 supports AOD mode for low-power clock/notification display.

---

## LVGL Thread Safety Rules (Single-Core ESP32-C6)

| Where called from   | Safe operations                          | Unsafe                     |
|---|---|---|
| Before LVGL task    | Everything — lv_timer_create, layout ops | —                          |
| Inside lv_timer cb  | Everything                               | —                          |
| FreeRTOS tasks      | NOTHING directly — use shared state + timer polling | All LVGL APIs |

**Golden rule**: Create timers and build layout in `ui_display_init()` (called before
`xTaskCreate(lvgl_task)`). Never call `lv_timer_create` from app_main after the
LVGL task has started.

---

## LVGL Task Setup

```c
static void lvgl_task(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}
// Priority 1, stack 16384 bytes (may need 20480 for complex UIs)
xTaskCreate(lvgl_task, "lvgl", 16384, NULL, 1, NULL);
```

---

## Draw Buffer Setup

```c
// 1/4 screen, double-buffered, DMA-capable
#define BUF_LINES (448 / 4)  // 112 lines
static lv_color_t *buf1 = heap_caps_malloc(368 * BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
static lv_color_t *buf2 = heap_caps_malloc(368 * BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);

lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 368 * BUF_LINES);

lv_disp_drv_init(&disp_drv);
disp_drv.hor_res = 368;
disp_drv.ver_res = 448;
disp_drv.flush_cb = amoled_flush_cb;
disp_drv.rounder_cb = lvgl_rounder_cb;  // Even-alignment required
disp_drv.draw_buf = &draw_buf;
lv_disp_drv_register(&disp_drv);
```

---

## Common Pitfalls

1. **Display blank** — TCA9554 P4 not set HIGH. Check IO expander init.
2. **Garbage on screen** — SH8601 coordinates not even-aligned. Add rounder callback.
3. **Touch not responding** — TCA9554 P5 not set HIGH, or FT3168 not initialized after delay.
4. **Text invisible on dark background** — forgot `lv_obj_set_style_text_color(..., LV_PART_ITEMS)`.
5. **Timer never fires** — `lv_timer_create` called after LVGL task started.
6. **Blank screen after mutation from FreeRTOS task** — use shared state + timer polling.
7. **`LV_COLOR_MAKE` compile error** — use `lv_color_make()` lowercase.
8. **Poor touch accuracy** — coordinates may need X/Y swap or inversion depending on rotation.
