# sprite_factory — declarative sprite bundle builder

Turns YAML sprite specs into a binary asset bundle for PixelPet.

## Run

```
pip install pillow pyyaml
python3 build_assets.py --src . --out-bin ../../build/assets.bin --out-header ../../projects/17_pixelpet/main/asset_index.h
```

## Sprite spec

```yaml
name: bubble_rise
palette: aqua            # named palette in palettes/*.yml
size: 8x8
frames: 6
durations_ms: [120, 120, 140, 160, 200, 100]
animate:
  base:
    - circle: {center: [4, 4], radius: 2, color: cyan}
    - pixel:  {pos: [3, 3], color: white}
  per_frame:
    0: {dy:  0, scale: 1.00}
    1: {dy: -2, scale: 1.05}
    ...
```

## Output

- `assets.bin` — flash this to the `assets` partition (`idf.py partition-table`
  for layout)
- `asset_index.h` — generated C header with `ASSET_<name>` enum constants
