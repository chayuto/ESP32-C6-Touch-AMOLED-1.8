#!/usr/bin/env python3
"""
build_assets.py — turn YAML sprite specs into a flash-resident asset bundle.

Bundle layout:
    header (64 bytes)
    TOC   (64 bytes per entry)
    palette pool (64 bytes per palette)
    data pool (sprite frames + duration tables)

The bundle is mmap'd at runtime by the ESP32; sprites are read directly
from flash via lv_img_dsc_t pointers into the mmap region.

Usage:
    python3 build_assets.py --src tools/sprite_factory \\
                            --out-bin build/assets.bin \\
                            --out-header projects/17_pixelpet/main/asset_index.h
"""
from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path
from typing import Dict, List, Tuple

import yaml
from PIL import Image

from primitives import PRIMITIVES

# ── Format constants ───────────────────────────────────────────────────
MAGIC = 0x50585041            # 'PXPA'
VERSION = 1
HEADER_SIZE = 64
TOC_ENTRY_SIZE = 64
PALETTE_SIZE = 64             # 16 entries × ARGB8888

FORMAT_I4 = 0
FORMAT_RGB565A8 = 1


# ── Palette handling ───────────────────────────────────────────────────
class PaletteRegistry:
    """Loads named palettes from yml files; deduplicates and assigns IDs."""

    def __init__(self, palette_dir: Path):
        self._by_name: Dict[str, Dict[str, int]] = {}
        self._argb_lists: List[List[Tuple[int, int, int, int]]] = []
        self._name_to_id: Dict[str, int] = {}

        for f in sorted(palette_dir.glob("*.yml")):
            doc = yaml.safe_load(f.read_text())
            for name, spec in (doc or {}).items():
                self._by_name[name] = spec

    def resolve(self, name: str) -> int:
        if name in self._name_to_id:
            return self._name_to_id[name]

        if name not in self._by_name:
            raise KeyError(f"unknown palette '{name}'")

        spec = self._by_name[name]
        # spec is {color_name: "#RRGGBB" or "#AARRGGBB"} — at most 16 entries.
        # Index 0 is always transparent unless explicitly set.
        argb: List[Tuple[int, int, int, int]] = [(0, 0, 0, 0)] * 16
        for color_name, hex_code in spec.items():
            idx_str, _, key = color_name.partition("/")
            try:
                idx = int(idx_str)
            except ValueError:
                raise ValueError(
                    f"palette '{name}' entry '{color_name}': "
                    f"expected '<index>/<label>' or '<index>'") from None
            if idx < 0 or idx >= 16:
                raise ValueError(f"palette '{name}': index {idx} out of range")
            argb[idx] = self._hex_to_argb(hex_code)

        pid = len(self._argb_lists)
        self._argb_lists.append(argb)
        self._name_to_id[name] = pid

        # Build colour-name → index map for sprite specs that say color: cyan.
        # Stored on the registry so build can look it up later.
        self._color_lookup_for_palette(name)
        return pid

    def _color_lookup_for_palette(self, name: str) -> Dict[str, int]:
        spec = self._by_name[name]
        lookup: Dict[str, int] = {}
        for color_name, _hex in spec.items():
            idx_str, _, key = color_name.partition("/")
            idx = int(idx_str)
            if key:
                lookup[key] = idx
            lookup[idx_str] = idx
        return lookup

    def color_index(self, palette_name: str, color: str | int) -> int:
        if isinstance(color, int):
            return color
        return self._color_lookup_for_palette(palette_name)[color]

    @staticmethod
    def _hex_to_argb(h: str) -> Tuple[int, int, int, int]:
        h = h.lstrip("#")
        if len(h) == 6:                   # RGB → opaque
            r = int(h[0:2], 16)
            g = int(h[2:4], 16)
            b = int(h[4:6], 16)
            return (255, r, g, b)
        if len(h) == 8:                   # AARRGGBB
            a = int(h[0:2], 16)
            r = int(h[2:4], 16)
            g = int(h[4:6], 16)
            b = int(h[6:8], 16)
            return (a, r, g, b)
        raise ValueError(f"bad hex color '{h}'")

    def palette_bytes(self, palette_id: int) -> bytes:
        """64 bytes of ARGB8888 (LVGL 9 I4 layout) for one palette."""
        out = bytearray()
        for (a, r, g, b) in self._argb_lists[palette_id]:
            out += struct.pack("<BBBB", b, g, r, a)
        return bytes(out)

    @property
    def count(self) -> int:
        return len(self._argb_lists)


# ── Sprite rendering ───────────────────────────────────────────────────
def parse_size(s: str) -> Tuple[int, int]:
    w, _, h = s.partition("x")
    return int(w), int(h)


def render_frame(spec: dict, frame_idx: int, size: Tuple[int, int],
                 palette: PaletteRegistry, palette_name: str) -> Image.Image:
    """Render one frame of a sprite. Returns a P-mode image with palette indices.

    YAML can express the frame in three ways:
      1. spec["shape"] — single static shape used for all frames.
      2. spec["animate"]["base"] — default shape, per_frame entries can
         apply dx/dy translation (cheap "the whole thing shifts").
      3. spec["frames_shape"][i] — list of shape arrays, one per frame
         (used when each frame is structurally different, e.g. body
         breathing).
    """
    w, h = size
    img = Image.new("P", (w, h), 0)        # default index 0 = transparent

    # Frame 3: per-frame complete shape array
    frames_shape = spec.get("frames_shape")
    if frames_shape is not None:
        shapes = frames_shape[frame_idx]
        dx = dy = 0
    else:
        animate = spec.get("animate", {})
        base = animate.get("base", []) or spec.get("shape", [])
        per_frame = (animate.get("per_frame", {}) or {}).get(frame_idx, {})
        shapes = per_frame.get("shape") if isinstance(per_frame, dict) else None
        if shapes is None:
            shapes = base
        dx = per_frame.get("dx", 0) if isinstance(per_frame, dict) else 0
        dy = per_frame.get("dy", 0) if isinstance(per_frame, dict) else 0

    for op_dict in shapes:
        ((op_name, op_args),) = op_dict.items()
        op_args = dict(op_args)
        # Translate any positional fields by per-frame dx/dy
        for k in ("center", "pos", "start", "end"):
            if k in op_args and isinstance(op_args[k], list):
                op_args[k] = [op_args[k][0] + dx, op_args[k][1] + dy]
        # Resolve color by name
        if "color" in op_args:
            op_args["color"] = palette.color_index(palette_name, op_args["color"])
        # Resolve top/bottom for gradient
        for k in ("top", "bottom"):
            if k in op_args:
                op_args[k] = palette.color_index(palette_name, op_args[k])

        fn = PRIMITIVES.get(op_name)
        if fn is None:
            raise ValueError(f"unknown primitive '{op_name}'")
        fn(img, **op_args)

    return img


def img_to_i4(img: Image.Image) -> bytes:
    """Pack a P-mode image (16-color indexed) into LVGL I4 byte stream.

    Two pixels per byte, high nibble = first (left) pixel.
    """
    out = bytearray()
    pixels = list(img.getdata())
    w = img.width
    for y in range(img.height):
        row = pixels[y * w:(y + 1) * w]
        # Pad to even width
        if w & 1:
            row.append(0)
        for i in range(0, len(row), 2):
            hi = row[i] & 0x0F
            lo = row[i + 1] & 0x0F
            out.append((hi << 4) | lo)
    return bytes(out)


# ── Bundle builder ─────────────────────────────────────────────────────
class BundleBuilder:
    def __init__(self, palette_reg: PaletteRegistry):
        self.palettes = palette_reg
        self.entries: List[dict] = []
        self.data_pool = bytearray()

    debug_dir: Path | None = None  # set by main()

    def add_sprite(self, name: str, spec: dict) -> None:
        size = parse_size(spec["size"])
        frame_count = spec.get("frames", 1)
        palette_name = spec["palette"]
        palette_id = self.palettes.resolve(palette_name)
        palette_bytes = self.palettes.palette_bytes(palette_id)
        upscale = int(spec.get("upscale", 1))

        # Native size used for primitive rendering. After rendering, each
        # frame is upscaled (nearest-neighbour) to (w*upscale, h*upscale)
        # so the on-device image is bigger without LVGL zoom games.
        nat_w, nat_h = size
        w, h = nat_w * upscale, nat_h * upscale

        frame_bytes = (w * h + 1) // 2     # I4 packed
        per_frame_block = 64 + frame_bytes  # palette + pixels

        # Per-sprite data pool layout (LVGL 9 I4 frame-per-block):
        #   [ palette || frame_0_pixels ]   ← TOC.data_offset points here
        #   [ palette || frame_1_pixels ]
        #   ...
        # Cost: 64 bytes palette per frame. For 240 sprites × 6 frames
        # ≈ 92 KB extra in flash — bearable. Avoids any runtime
        # decoder work; LVGL 9 reads each block directly via I4.
        data_offset_in_pool = len(self.data_pool)
        for f in range(frame_count):
            img = render_frame(spec, f, size, self.palettes, palette_name)
            if upscale > 1:
                # Pillow's resize on a P-mode image with NEAREST keeps the
                # palette indices intact (no quantization needed afterwards).
                img = img.resize((w, h), Image.Resampling.NEAREST)
            if self.debug_dir is not None:
                # Apply the palette so the dumped PNG is viewable.
                pal = self.palettes._argb_lists[palette_id]
                rgb = [(r, g, b) for (a, r, g, b) in pal for _ in (0,)]
                flat = []
                for (a, r, g, b) in pal:
                    flat.extend([r, g, b])
                img.putpalette(flat[:768])
                out_dir = self.debug_dir / name.replace("/", "_")
                out_dir.mkdir(parents=True, exist_ok=True)
                img.save(out_dir / f"{f:02d}.png")
            self.data_pool += palette_bytes
            self.data_pool += img_to_i4(img)

        sprite_data_size = per_frame_block * frame_count

        durations_offset_in_pool = 0
        durations_ms = spec.get("durations_ms")
        if durations_ms:
            if len(durations_ms) != frame_count:
                raise ValueError(
                    f"{name}: durations_ms has {len(durations_ms)} entries "
                    f"but {frame_count} frames")
            durations_offset_in_pool = len(self.data_pool)
            self.data_pool += struct.pack(f"<{frame_count}H", *durations_ms)

        # Pad to 4-byte alignment
        while len(self.data_pool) & 3:
            self.data_pool.append(0)

        self.entries.append({
            "name": name,
            "format": FORMAT_I4,
            "width": w,
            "height": h,
            "frame_count": frame_count,
            "palette_id": palette_id,
            "data_pool_offset": data_offset_in_pool,
            "data_size": sprite_data_size,
            "durations_pool_offset": durations_offset_in_pool,
            "per_frame_block": per_frame_block,
        })

    def build(self) -> bytes:
        toc_offset = HEADER_SIZE
        toc_size = TOC_ENTRY_SIZE * len(self.entries)
        data_pool_offset = toc_offset + toc_size
        total_size = data_pool_offset + len(self.data_pool)

        out = bytearray(total_size)

        # Header — 64 bytes:
        #   0  uint32 magic
        #   4  uint16 version
        #   6  uint16 flags
        #   8  uint32 toc_offset
        #  12  uint32 toc_count
        #  16  uint32 data_pool_offset
        #  20  uint32 data_pool_size
        #  24  uint32 total_size
        #  28  uint32 reserved
        #  32  uint32 crc32 (filled in below after data is in place)
        #  36  24 bytes pad → 64
        struct.pack_into(
            "<IHHIIIIIII24x",
            out, 0,
            MAGIC, VERSION, 0,
            toc_offset, len(self.entries),
            data_pool_offset, len(self.data_pool),
            total_size, 0, 0,
        )

        # TOC — 64 bytes per entry. Palette is inlined at the start of
        # each sprite's data block (LVGL 9 I4 layout), so the palette_id
        # field is debug-only.
        for i, e in enumerate(self.entries):
            base = toc_offset + i * TOC_ENTRY_SIZE
            name_bytes = e["name"].encode("utf-8")[:31].ljust(32, b"\x00")
            struct.pack_into("<32s", out, base, name_bytes)
            struct.pack_into(
                "<HHHHHH",
                out, base + 32,
                e["format"], 0, e["width"], e["height"],
                e["frame_count"], e["palette_id"],
            )
            struct.pack_into(
                "<III4x",
                out, base + 44,
                data_pool_offset + e["data_pool_offset"],
                e["data_size"],
                (data_pool_offset + e["durations_pool_offset"]
                 if e["durations_pool_offset"] else 0),
            )

        # Data pool
        out[data_pool_offset:data_pool_offset + len(self.data_pool)] = (
            self.data_pool)

        # CRC over everything after the CRC field (offset 36)
        crc = zlib.crc32(bytes(out[36:])) & 0xFFFFFFFF
        struct.pack_into("<I", out, 32, crc)

        return bytes(out)


# ── Header generation ──────────────────────────────────────────────────
def write_header(path: Path, entries: List[dict]) -> None:
    lines = [
        "/* Auto-generated by tools/sprite_factory/build_assets.py — do not edit */",
        "#pragma once",
        "",
        "typedef enum {",
    ]
    for i, e in enumerate(entries):
        sym = "ASSET_" + e["name"].upper().replace("/", "_").replace("-", "_")
        lines.append(f"    {sym} = {i},")
    lines.append(f"    ASSET_COUNT = {len(entries)},")
    lines.append("} asset_id_t;")
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines))


# ── Main ───────────────────────────────────────────────────────────────
def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, type=Path,
                    help="sprite_factory directory (palettes/, sprites/)")
    ap.add_argument("--out-bin", required=True, type=Path)
    ap.add_argument("--out-header", required=True, type=Path)
    ap.add_argument("--debug-pngs", type=Path, default=None,
                    help="Optional dir to dump each rendered frame as PNG.")
    args = ap.parse_args()

    src: Path = args.src
    palette_dir = src / "palettes"
    sprite_dir = src / "sprites"

    if not palette_dir.exists():
        print(f"missing palette dir {palette_dir}", file=sys.stderr)
        return 1
    if not sprite_dir.exists():
        print(f"missing sprite dir {sprite_dir}", file=sys.stderr)
        return 1

    palettes = PaletteRegistry(palette_dir)
    builder = BundleBuilder(palettes)
    builder.debug_dir = args.debug_pngs

    # Walk sprites/, treating subdir as namespace prefix.
    # YAML may set palette_variants: [pal1, pal2, ...] to emit one sprite
    # per palette, with names suffixed by the palette's short tag (the
    # part after "species_" if present, else the full palette name).
    for f in sorted(sprite_dir.rglob("*.yml")):
        rel = f.relative_to(sprite_dir).with_suffix("")
        name = "/".join(rel.parts)
        spec = yaml.safe_load(f.read_text())

        variants = spec.pop("palette_variants", None)
        if variants:
            for pal in variants:
                tag = pal.split("species_", 1)[-1]   # 'species_blob_pink' → 'blob_pink'
                # If the file is named bodies/blob_idle, the variant name
                # becomes bodies/blob_idle_pink (drop the rig prefix from
                # the tag so we don't repeat it).
                base_last = name.rsplit("/", 1)[-1]
                if tag.startswith(base_last.split("_", 1)[0] + "_"):
                    short = tag.split("_", 1)[1]    # 'blob_pink' → 'pink'
                else:
                    short = tag
                variant_name = f"{name}_{short}"
                variant_spec = {**spec, "palette": pal}
                try:
                    builder.add_sprite(variant_name, variant_spec)
                except Exception as e:
                    print(f"error in {f} (variant {pal}): {e}", file=sys.stderr)
                    return 1
        else:
            try:
                builder.add_sprite(name, spec)
            except Exception as e:
                print(f"error in {f}: {e}", file=sys.stderr)
                return 1

    bundle = builder.build()
    args.out_bin.parent.mkdir(parents=True, exist_ok=True)
    args.out_bin.write_bytes(bundle)
    write_header(args.out_header, builder.entries)

    print(f"[sprite_factory] {len(builder.entries)} sprites, "
          f"{builder.palettes.count} palettes, "
          f"{len(bundle)} bytes -> {args.out_bin}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
