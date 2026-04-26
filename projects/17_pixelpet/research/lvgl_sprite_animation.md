# LVGL sprite animation on ESP32-C6 — research notes

> Reference for the PixelPet animation rework. Captures what's known about
> sprite formats, animation widgets, the asset pipeline, and per-frame
> performance budget on this board (368×448 AMOLED, single-core RISC-V at
> 160 MHz, ~290 KB free heap, no PSRAM).
>
> Date: 2026-04-26. Re-validate against current LVGL releases before
> making big asset-format decisions.

---

## 1. Indexed image format (LVGL 8 vs 9)

**LVGL 8.x** uses `LV_IMG_CF_INDEXED_4BIT`. Memory layout: 16 palette
entries (each `lv_color_t` + `lv_opa_t`, so RGB565+alpha or RGB888+alpha
depending on `LV_COLOR_DEPTH`) followed by packed 4-bit pixel indices,
two pixels per byte. Header set in `lv_img_dsc_t.header.cf`. ~64 bytes
palette + W*H/2 pixel data.

**LVGL 9.x** renames to `LV_COLOR_FORMAT_I4`. Palette entries are always
**ARGB8888 (4 bytes each, 64 bytes total)** regardless of display color
depth. Requires `LV_DRAW_SW_SUPPORT_ARGB8888=1` in `lv_conf.h`.

### Critical gotcha — LVGL 9 I4 memory regression

[lvgl/lvgl#6907](https://github.com/lvgl/lvgl/issues/6907): the renderer
allocates a **4-byte-per-pixel intermediate buffer** when blending I4
sources. A 64×64 I4 sprite that should cost 2 KB temporarily uses
~16 KB during draw.

Implication on this board: with ~290 KB free heap, sprites under
~128×128 are still fine, but the format becomes unviable for full-screen
indexed canvases. Don't use I4 for backgrounds larger than ~160×160.

The `espressif/lvgl_lvgl` managed component is currently 9.2.x — assume
the 9.x penalty applies.

---

## 2. Frame animation pattern

Three options, ranked by efficiency:

1. **`lv_animimg`** *(use this)* — built-in widget, takes
   `const void **` array of `lv_img_dsc_t*`, single timer drives the
   index. Lowest overhead, no per-frame mallocs. Available in 8.2+ and
   9.x. Canonical example:
   [`lv_example_animimg_1.c`](https://github.com/lvgl/lvgl/blob/master/examples/widgets/animimg/lv_example_animimg_1.c).
2. **`lv_anim_t` + custom `exec_cb`** calling `lv_image_set_src()` —
   equivalent CPU cost, more code; only useful for non-linear timing
   (eased blink, ping-pong).
3. **Manual `lv_image_set_src()` from a `lv_timer`** — same draw cost,
   slightly more allocation churn because each `set_src` invalidates
   the cache entry.

All three trigger an LVGL invalidate + redraw of the image's bounding
box. Per-frame CPU is dominated by the blit, not the dispatch.

---

## 3. PNG → C array tooling

- **[`scripts/LVGLImage.py`](https://github.com/lvgl/lvgl/blob/master/scripts/LVGLImage.py)**
  is the current canonical converter (separate forks per LVGL major
  version). Supports `--ofmt C --cf I4` and LZ4 compression, batch
  conversion of a directory.
- **[Online converter](https://lvgl.io/tools/imageconverter)** wraps
  the same script — fine for one-offs but
  [#7208](https://github.com/lvgl/lvgl/issues/7208) reorders palette
  entries, breaking palette-swap animation.
- **No first-party Aseprite plugin exists.**

### Recommended pipeline

```
tools/sprites/<name>/<frame>.png   — Aseprite CLI export
        │
        ▼
CMake add_custom_command — re-runs LVGLImage.py --cf I4 when PNG changes
        │
        ▼
main/generated_sprites/<name>.c    — committed? No: regenerate from PNG
        │
        ▼
linked into the app build
```

Aseprite headless export:
```bash
aseprite -b walk.aseprite --save-as walk_{frame}.png
```

[`spriteanvil`](https://github.com/aschoelzhorn/spriteanvil) handles
RGB565/ARGB but not LVGL's indexed format — not what we want.

---

## 4. ESP32 + LVGL pixel-art reference projects (2023+)

| Project | Use | Format | Notes |
|---|---|---|---|
| [CyberXcyborg/ESP32-TamaPetchi](https://github.com/CyberXcyborg/ESP32-TamaPetchi) | Tamagotchi clone | TFT_eSPI sprites (not LVGL) | Useful for sprite art and state-machine design only |
| [fbiego/esp32-lvgl-watchface](https://github.com/fbiego/esp32-lvgl-watchface) | Watchface engine | RGB565+alpha | Converts binary GTR/Mi-Band watchfaces to LVGL C arrays; cycled via custom timer. Good convert-and-bake reference. |
| [grinchdubs/tdESP32display](https://github.com/grinchdubs/tdESP32display) | Pixel-art player on ESP32-P4 | WebP/GIF decode | Background-task decode + double-buffer prefetch — pattern translates to C6 |
| [RBEGamer/TamagotchiESP32](https://github.com/RBEGamer/TamagotchiESP32) | Original Bandai ROM emulator | Emulated LCD bitmaps | Older, but good for reference cadence |

**Conclusion:** none of the actively-maintained ESP32 virtual-pet repos
use LVGL `lv_animimg` + I4. There's a clear opening to build a clean
reference here.

---

## 5. Sprite size budget on this chip

Measured rules-of-thumb from the
[esp-bsp performance docs](https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/docs/performance.md)
and the
[Seeed XIAO-S3 LVGL guide](https://wiki.seeedstudio.com/round_display_animation_workshop/),
adjusted for the C6's lack of PIE SIMD (~2× slower than S3 per pixel):

| Sprite | At 30 fps | Verdict |
|---|---|---|
| 64×64 I4 | trivial — ~4 KB pixel cost, ~12 KB working memory | run 4–6 concurrently |
| 128×128 I4 | borderline — ~16 KB pixel cost, ~64 KB intermediate | one is fine, two drops to ~15–20 fps |
| 160×160 mixed UI | hard ceiling — ~25,000 px/frame | one or four 80×80 |

### RGB565+A8 alternative

~30 % faster to draw than I4 in LVGL 9 (no palette lookup, no
intermediate buffer), but doubles flash footprint. With a 3 MB app
partition this is rarely the binding constraint.

**Recommendation:** RGB565+A8 for sprites under 64×64 (the pet character
itself); reserve I4 for 128×128+ where flash savings matter
(backgrounds, scene tiles).

---

## 6. Concrete recommendation for PixelPet

Use **LVGL 9 + `lv_animimg`** with **RGB565+A8** sprites for the pet
character (≤64×64) and **I4 indexed** for larger background/scene
tiles. Build the asset pipeline as Aseprite → per-frame PNG →
`LVGLImage.py` → CMake-generated C arrays.

Pre-allocate **one `lv_animimg` per active sprite layer**; drive
idle/eat/sleep state changes by swapping the source array, not by
destroying/recreating widgets.

---

## Sources

- [LVGL 9.6 Color Formats](https://docs.lvgl.io/master/main-modules/images/color_formats.html)
- [LVGL 8 Images doc](https://docs.lvgl.io/8.0/overview/image.html)
- [`lv_animimg` widget (9.6)](https://docs.lvgl.io/master/widgets/animimg.html)
- [`lv_example_animimg_1.c`](https://github.com/lvgl/lvgl/blob/master/examples/widgets/animimg/lv_example_animimg_1.c)
- [`scripts/LVGLImage.py`](https://github.com/lvgl/lvgl/blob/master/scripts/LVGLImage.py)
- [LVGL #6907 — I4 memory regression in v9](https://github.com/lvgl/lvgl/issues/6907)
- [LVGL #7208 — palette reorder bug](https://github.com/lvgl/lvgl/issues/7208)
- [esp-bsp LVGL performance notes](https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/docs/performance.md)
- [Seeed XIAO ESP32-S3 LVGL animation tuning](https://wiki.seeedstudio.com/round_display_animation_workshop/)
- [fbiego/esp32-lvgl-watchface](https://github.com/fbiego/esp32-lvgl-watchface)
- [grinchdubs/tdESP32display](https://github.com/grinchdubs/tdESP32display)
- [CyberXcyborg/ESP32-TamaPetchi](https://github.com/CyberXcyborg/ESP32-TamaPetchi)
