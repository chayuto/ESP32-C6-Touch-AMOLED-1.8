"""
primitives.py — small drawing primitives for procedural sprite generation.

Each primitive renders into a PIL Image (P-mode, 16-color indexed) by
plotting palette indices directly. The palette is an external concern;
primitives only deal with index numbers.
"""
from __future__ import annotations

import math
from PIL import Image, ImageDraw


def _set(img: Image.Image, x: int, y: int, idx: int) -> None:
    if 0 <= x < img.width and 0 <= y < img.height:
        img.putpixel((x, y), idx)


def circle(img: Image.Image, *, center, radius: int, color: int,
           filled: bool = True) -> None:
    cx, cy = center
    r = radius
    r2 = r * r
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            d = dx * dx + dy * dy
            if filled and d <= r2:
                _set(img, cx + dx, cy + dy, color)
            elif not filled and (r - 1) ** 2 < d <= r2:
                _set(img, cx + dx, cy + dy, color)


def rect(img: Image.Image, *, pos, size, color: int,
         filled: bool = True) -> None:
    x, y = pos
    w, h = size
    if filled:
        for dy in range(h):
            for dx in range(w):
                _set(img, x + dx, y + dy, color)
    else:
        for dx in range(w):
            _set(img, x + dx, y, color)
            _set(img, x + dx, y + h - 1, color)
        for dy in range(h):
            _set(img, x, y + dy, color)
            _set(img, x + w - 1, y + dy, color)


def oval(img: Image.Image, *, pos, size, color: int) -> None:
    """Filled axis-aligned ellipse."""
    x, y = pos
    w, h = size
    rx, ry = w / 2.0, h / 2.0
    cx, cy = x + rx - 0.5, y + ry - 0.5
    for dy in range(h):
        for dx in range(w):
            nx = (dx + 0.5 - rx) / rx if rx else 0
            ny = (dy + 0.5 - ry) / ry if ry else 0
            if nx * nx + ny * ny <= 1.0:
                _set(img, x + dx, y + dy, color)


def line(img: Image.Image, *, start, end, color: int) -> None:
    x0, y0 = start
    x1, y1 = end
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx + dy
    while True:
        _set(img, x0, y0, color)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def pixel(img: Image.Image, *, pos, color: int) -> None:
    _set(img, pos[0], pos[1], color)


def fill(img: Image.Image, *, color: int) -> None:
    for y in range(img.height):
        for x in range(img.width):
            _set(img, x, y, color)


def gradient_v(img: Image.Image, *, top: int, bottom: int) -> None:
    """Vertical band gradient using indices top..bottom inclusive across height."""
    h = img.height
    span = bottom - top
    if span == 0:
        fill(img, color=top)
        return
    for y in range(h):
        idx = top + round(span * y / max(1, h - 1))
        for x in range(img.width):
            _set(img, x, y, idx)


# Registry — maps YAML keys to drawing functions.
PRIMITIVES = {
    "circle":     circle,
    "rect":       rect,
    "oval":       oval,
    "line":       line,
    "pixel":      pixel,
    "fill":       fill,
    "gradient_v": gradient_v,
}
