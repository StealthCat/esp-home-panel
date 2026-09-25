#!/usr/bin/env python3
"""Generate clean transparent LVGL theme artwork.

The approved mockup SVGs remain the visual reference for composition, palette,
and motifs, but generated sprites are not cropped from them. Every emitted
theme image is redrawn into a fresh RGBA buffer and written as an ARGB8888 LVGL
descriptor.

The generated include is intentionally not tracked. Normal firmware builds do
not need an image decoder at runtime.
"""

from __future__ import annotations

import hashlib
import math
from pathlib import Path

try:
    Import("env")  # type: ignore[name-defined]
    ROOT = Path(env.subst("$PROJECT_DIR")).resolve()
except NameError:
    ROOT = Path(__file__).resolve().parents[1]

OUT = ROOT / "main" / "theme_art_generated.inc"
WIDTH = 800
HEIGHT = 480
GENERATOR_VERSION = "theme-art-v10-all-procedural-argb"

THEMES = {
    "kittens": ROOT / "assets" / "kittens-pink" / "screen.svg",
    "soccer": ROOT / "assets" / "soccer-green" / "screen.svg",
    "magic": ROOT / "assets" / "magic-bricks-blue" / "screen.svg",
}

# The canonical SVGs are visual references only. No emitted sprite is cropped
# or sampled from them.


def _canvas(w: int, h: int) -> bytearray:
    return bytearray(w * h * 4)


def _pixel(img: bytearray, w: int, h: int, x: int, y: int,
           color: tuple[int, int, int, int]) -> None:
    if x < 0 or y < 0 or x >= w or y >= h:
        return
    p = (y * w + x) * 4
    img[p:p + 4] = bytes(color)


def _fill_rect(img: bytearray, w: int, h: int,
               x0: int, y0: int, x1: int, y1: int,
               color: tuple[int, int, int, int]) -> None:
    for y in range(max(0, y0), min(h, y1)):
        for x in range(max(0, x0), min(w, x1)):
            _pixel(img, w, h, x, y, color)


def _fill_circle(img: bytearray, w: int, h: int,
                 cx: float, cy: float, radius: float,
                 color: tuple[int, int, int, int]) -> None:
    r2 = radius * radius
    x0 = max(0, int(math.floor(cx - radius)))
    x1 = min(w - 1, int(math.ceil(cx + radius)))
    y0 = max(0, int(math.floor(cy - radius)))
    y1 = min(h - 1, int(math.ceil(cy + radius)))
    for y in range(y0, y1 + 1):
        dy = (y + 0.5) - cy
        for x in range(x0, x1 + 1):
            dx = (x + 0.5) - cx
            if dx * dx + dy * dy <= r2:
                _pixel(img, w, h, x, y, color)


def _fill_ellipse(img: bytearray, w: int, h: int,
                  cx: float, cy: float, rx: float, ry: float,
                  color: tuple[int, int, int, int]) -> None:
    x0 = max(0, int(math.floor(cx - rx)))
    x1 = min(w - 1, int(math.ceil(cx + rx)))
    y0 = max(0, int(math.floor(cy - ry)))
    y1 = min(h - 1, int(math.ceil(cy + ry)))
    for y in range(y0, y1 + 1):
        dy = ((y + 0.5) - cy) / ry
        for x in range(x0, x1 + 1):
            dx = ((x + 0.5) - cx) / rx
            if dx * dx + dy * dy <= 1.0:
                _pixel(img, w, h, x, y, color)


def _fill_polygon(img: bytearray, w: int, h: int,
                  points: list[tuple[float, float]],
                  color: tuple[int, int, int, int]) -> None:
    min_y = max(0, int(math.floor(min(p[1] for p in points))))
    max_y = min(h - 1, int(math.ceil(max(p[1] for p in points))))
    n = len(points)

    for y in range(min_y, max_y + 1):
        scan_y = y + 0.5
        xs: list[float] = []
        for i in range(n):
            x1, y1 = points[i]
            x2, y2 = points[(i + 1) % n]
            if y1 == y2:
                continue
            if scan_y < min(y1, y2) or scan_y >= max(y1, y2):
                continue
            t = (scan_y - y1) / (y2 - y1)
            xs.append(x1 + t * (x2 - x1))

        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            xa = max(0, int(math.ceil(xs[i] - 0.5)))
            xb = min(w - 1, int(math.floor(xs[i + 1] - 0.5)))
            for x in range(xa, xb + 1):
                _pixel(img, w, h, x, y, color)


def _draw_line(img: bytearray, w: int, h: int,
               x1: float, y1: float, x2: float, y2: float,
               thickness: float,
               color: tuple[int, int, int, int]) -> None:
    dx = x2 - x1
    dy = y2 - y1
    steps = max(1, int(math.ceil(max(abs(dx), abs(dy)))))
    radius = max(0.5, thickness / 2.0)
    for i in range(steps + 1):
        t = i / steps
        _fill_circle(img, w, h, x1 + dx * t, y1 + dy * t, radius, color)


def _draw_circle_outline(img: bytearray, w: int, h: int,
                         cx: float, cy: float, radius: float,
                         thickness: float,
                         color: tuple[int, int, int, int]) -> None:
    steps = max(48, int(math.ceil(2.0 * math.pi * radius)))
    dot_radius = max(0.5, thickness / 2.0)
    for i in range(steps):
        angle = (2.0 * math.pi * i) / steps
        x = cx + math.cos(angle) * radius
        y = cy + math.sin(angle) * radius
        _fill_circle(img, w, h, x, y, dot_radius, color)


def _star_points(cx: float, cy: float, outer: float, inner: float,
                 points: int = 5, rotation_deg: float = -90.0) -> list[tuple[float, float]]:
    out: list[tuple[float, float]] = []
    for i in range(points * 2):
        radius = outer if i % 2 == 0 else inner
        angle = math.radians(rotation_deg + i * 180.0 / points)
        out.append((cx + math.cos(angle) * radius,
                    cy + math.sin(angle) * radius))
    return out


def _downsample_rgba(src: bytearray, sw: int, sh: int,
                     factor: int) -> tuple[bytearray, int, int]:
    dw = sw // factor
    dh = sh // factor
    out = bytearray(dw * dh * 4)
    samples = factor * factor

    for y in range(dh):
        for x in range(dw):
            alpha_sum = 0
            rgb_weighted = [0, 0, 0]

            for sy in range(factor):
                for sx in range(factor):
                    p = (((y * factor + sy) * sw) + (x * factor + sx)) * 4
                    alpha = src[p + 3]
                    alpha_sum += alpha
                    rgb_weighted[0] += src[p] * alpha
                    rgb_weighted[1] += src[p + 1] * alpha
                    rgb_weighted[2] += src[p + 2] * alpha

            d = (y * dw + x) * 4
            if alpha_sum:
                out[d] = int(round(rgb_weighted[0] / alpha_sum))
                out[d + 1] = int(round(rgb_weighted[1] / alpha_sum))
                out[d + 2] = int(round(rgb_weighted[2] / alpha_sum))
            out[d + 3] = int(round(alpha_sum / samples))

    return out, dw, dh


def _rgba(hex_rgb: int, alpha: int = 255) -> tuple[int, int, int, int]:
    return (
        (hex_rgb >> 16) & 0xFF,
        (hex_rgb >> 8) & 0xFF,
        hex_rgb & 0xFF,
        alpha,
    )


def make_header_icon(kind: str) -> tuple[bytearray, int, int]:
    """Create a 56x56 transparent theme emblem using the same ARGB sprite path.

    Icons are drawn at 2x resolution and downsampled for smoother edges.
    """
    scale = 2
    w = h = 56 * scale
    img = _canvas(w, h)

    def C(hex_rgb: int, a: int = 255) -> tuple[int, int, int, int]:
        return (
            (hex_rgb >> 16) & 0xFF,
            (hex_rgb >> 8) & 0xFF,
            hex_rgb & 0xFF,
            a,
        )

    if kind == "classic":
        # A compact smart-control panel / circuit badge.
        cyan = C(0x21A7FF)
        cyan_soft = C(0x8ED5FF)
        navy = C(0x082033, 242)
        _fill_rect(img, w, h, 24, 24, 88, 88, cyan)
        _fill_rect(img, w, h, 30, 30, 82, 82, navy)
        _fill_rect(img, w, h, 40, 39, 72, 58, cyan_soft)
        _fill_rect(img, w, h, 43, 42, 69, 55, C(0x0B1D31))
        for px in (30, 46, 62, 78):
            _fill_rect(img, w, h, px, 14, px + 6, 24, cyan)
            _fill_rect(img, w, h, px, 88, px + 6, 98, cyan)
        for py in (30, 46, 62, 78):
            _fill_rect(img, w, h, 14, py, 24, py + 6, cyan)
            _fill_rect(img, w, h, 88, py, 98, py + 6, cyan)
        _fill_circle(img, w, h, 44, 72, 5, C(0x34E66A))
        _fill_circle(img, w, h, 60, 72, 5, C(0xFFD447))
        _fill_circle(img, w, h, 76, 72, 5, C(0x8A5CFF))

    elif kind == "soccer":
        # Clean soccer ball emblem, independent of the old gear-bearing source crop.
        black = C(0x06120B)
        white = C(0xF4FFF7)
        green = C(0x39EB69)
        _fill_circle(img, w, h, 56, 56, 49, green)
        _fill_circle(img, w, h, 56, 56, 45, black)
        _fill_circle(img, w, h, 56, 56, 41, white)
        center = _star_points(56, 56, 15, 12, points=5, rotation_deg=-90)
        _fill_polygon(img, w, h, center, black)
        patches = [
            (56, 17), (92, 43), (78, 88), (34, 88), (20, 43)
        ]
        for px, py in patches:
            patch = _star_points(px, py, 10, 8, points=5, rotation_deg=-90)
            _fill_polygon(img, w, h, patch, black)
            _draw_line(img, w, h, 56, 56, px, py, 3, black)

    elif kind == "kittens":
        # Friendly cat face using the theme's pink/cream palette.
        outline = C(0xFF69AA)
        fur = C(0xFFF0F6)
        ear = C(0xFFB1CC)
        dark = C(0x5A3041)
        _fill_polygon(img, w, h, [(24, 45), (30, 12), (48, 34)], outline)
        _fill_polygon(img, w, h, [(64, 34), (82, 12), (88, 45)], outline)
        _fill_polygon(img, w, h, [(29, 41), (33, 20), (45, 36)], ear)
        _fill_polygon(img, w, h, [(67, 36), (79, 20), (83, 41)], ear)
        _fill_circle(img, w, h, 56, 59, 36, outline)
        _fill_circle(img, w, h, 56, 59, 31, fur)
        _fill_circle(img, w, h, 43, 53, 4, dark)
        _fill_circle(img, w, h, 69, 53, 4, dark)
        _fill_polygon(img, w, h, [(56, 61), (50, 67), (62, 67)], ear)
        _draw_line(img, w, h, 56, 67, 56, 73, 2, dark)
        _draw_line(img, w, h, 56, 73, 49, 78, 2, dark)
        _draw_line(img, w, h, 56, 73, 63, 78, 2, dark)
        for yy in (64, 72):
            _draw_line(img, w, h, 46, yy, 20, yy - 4, 2, outline)
            _draw_line(img, w, h, 66, yy, 92, yy - 4, 2, outline)

    elif kind == "magic":
        # Gold wand and stars matching the blue/gold magic theme.
        gold = C(0xE2B860)
        gold2 = C(0xF5C95A)
        blue = C(0x2FA8FF)
        _draw_line(img, w, h, 30, 84, 76, 38, 10, gold)
        _draw_line(img, w, h, 29, 85, 75, 39, 4, C(0xFFF2C2))
        _fill_polygon(img, w, h, _star_points(79, 31, 22, 9, 5), gold2)
        _fill_polygon(img, w, h, _star_points(30, 29, 10, 4, 5), blue)
        _fill_polygon(img, w, h, _star_points(86, 78, 8, 3, 5), blue)
        _fill_circle(img, w, h, 22, 59, 4, gold2)

    else:
        raise ValueError(f"Unknown header icon kind: {kind}")

    return _downsample_rgba(img, w, h, scale)



def make_fan_icon(kind: str) -> tuple[bytearray, int, int]:
    """Create a clean 142x116 transparent fan illustration for a theme."""
    scale = 2
    fw, fh = 142, 116
    w, h = fw * scale, fh * scale
    img = _canvas(w, h)

    def C(hex_rgb: int, a: int = 255) -> tuple[int, int, int, int]:
        return (
            (hex_rgb >> 16) & 0xFF,
            (hex_rgb >> 8) & 0xFF,
            hex_rgb & 0xFF,
            a,
        )

    if kind == "classic":
        ring = C(0x21A7FF)
        blade = C(0x8A5CFF)
        blade2 = C(0x8ED5FF)
        hub = C(0xF7FBFF)
    elif kind == "soccer":
        ring = C(0x39EB69)
        blade = C(0xB2FF59)
        blade2 = C(0xF4FFF7)
        hub = C(0x06120B)
    elif kind == "kittens":
        ring = C(0xFF69AA)
        blade = C(0xFFC0DA)
        blade2 = C(0xFFF0F6)
        hub = C(0x5A3041)
    elif kind == "magic":
        ring = C(0xE2B860)
        blade = C(0x2FA8FF)
        blade2 = C(0xF5C95A)
        hub = C(0xFFF2C2)
    else:
        raise ValueError(f"Unknown fan icon kind: {kind}")

    cx, cy = 58 * scale, 58 * scale
    outer = 50 * scale
    inner = 44 * scale

    # Theme-specific backplate decoration, kept within the fan footprint.
    if kind == "magic":
        shield = [
            (cx, 5 * scale),
            (104 * scale, 24 * scale),
            (96 * scale, 84 * scale),
            (cx, 111 * scale),
            (20 * scale, 84 * scale),
            (12 * scale, 24 * scale),
        ]
        _fill_polygon(img, w, h, shield, C(0x0D3266, 185))
        _draw_line(img, w, h, 58 * scale, 8 * scale, 102 * scale, 25 * scale,
                   3 * scale, ring)
        _draw_line(img, w, h, 102 * scale, 25 * scale, 94 * scale, 82 * scale,
                   3 * scale, ring)
        _draw_line(img, w, h, 94 * scale, 82 * scale, 58 * scale, 108 * scale,
                   3 * scale, ring)
        _draw_line(img, w, h, 58 * scale, 108 * scale, 22 * scale, 82 * scale,
                   3 * scale, ring)
        _draw_line(img, w, h, 22 * scale, 82 * scale, 14 * scale, 25 * scale,
                   3 * scale, ring)
        _draw_line(img, w, h, 14 * scale, 25 * scale, 58 * scale, 8 * scale,
                   3 * scale, ring)
    elif kind == "kittens":
        _fill_polygon(img, w, h,
                      [(20 * scale, 33 * scale), (30 * scale, 9 * scale),
                       (45 * scale, 31 * scale)], C(0xFFB1CC, 220))
        _fill_polygon(img, w, h,
                      [(71 * scale, 31 * scale), (86 * scale, 9 * scale),
                       (97 * scale, 33 * scale)], C(0xFFB1CC, 220))
    elif kind == "soccer":
        # Small ball badge to distinguish the soccer fan art.
        bx, by = 121 * scale, 23 * scale
        _fill_circle(img, w, h, bx, by, 16 * scale, C(0x39EB69))
        _fill_circle(img, w, h, bx, by, 13 * scale, C(0xF4FFF7))
        _fill_polygon(img, w, h,
                      _star_points(bx, by, 6 * scale, 4.5 * scale, 5),
                      C(0x06120B))

    # Ring.
    _fill_circle(img, w, h, cx, cy, outer, ring)
    _fill_circle(img, w, h, cx, cy, inner, C(0x000000, 0))

    # Four broad fan blades.
    for angle_deg in (0, 90, 180, 270):
        angle = math.radians(angle_deg)
        ca = math.cos(angle)
        sa = math.sin(angle)

        local = [
            (9, -12), (37, -18), (43, -7), (34, 9), (14, 13)
        ]
        pts = []
        for lx, ly in local:
            x = cx + (lx * ca - ly * sa) * scale
            y = cy + (lx * sa + ly * ca) * scale
            pts.append((x, y))
        _fill_polygon(img, w, h, pts, blade if angle_deg in (0, 180) else blade2)

    _fill_circle(img, w, h, cx, cy, 10 * scale, hub)
    _fill_circle(img, w, h, cx, cy, 5 * scale, ring)

    # Tiny theme accent that does not overlap live labels.
    if kind == "classic":
        for i, col in enumerate((0x34E66A, 0xFFD447, 0x8A5CFF)):
            _fill_circle(img, w, h, (116 + i * 9) * scale, 96 * scale,
                         3 * scale, C(col))
    elif kind == "kittens":
        # Paw at lower-right.
        px, py = 122 * scale, 94 * scale
        _fill_circle(img, w, h, px, py, 5 * scale, C(0xFF69AA))
        for ox, oy in ((-7, -6), (0, -9), (7, -6)):
            _fill_circle(img, w, h, px + ox * scale, py + oy * scale,
                         3 * scale, C(0xFF69AA))
    elif kind == "magic":
        _fill_polygon(img, w, h,
                      _star_points(122 * scale, 92 * scale,
                                   7 * scale, 3 * scale, 5),
                      C(0xF5C95A))

    return _downsample_rgba(img, w, h, scale)


def make_soccer_goal() -> tuple[bytearray, int, int]:
    """Create a clean 76x44 transparent soccer goal with no baked UI pixels."""
    scale = 2
    gw, gh = 76, 44
    w, h = gw * scale, gh * scale
    img = _canvas(w, h)

    green = (57, 235, 105, 220)
    white = (244, 255, 247, 215)

    x0, y0 = 4 * scale, 4 * scale
    x1, y1 = 71 * scale, 39 * scale

    _draw_line(img, w, h, x0, y0, x1, y0, 3 * scale, white)
    _draw_line(img, w, h, x0, y0, x0, y1, 3 * scale, white)
    _draw_line(img, w, h, x1, y0, x1, y1, 3 * scale, white)
    _draw_line(img, w, h, x0, y1, x1, y1, 2 * scale, green)

    for xx in range(14, 71, 12):
        _draw_line(img, w, h, xx * scale, y0, xx * scale, y1,
                   1.2 * scale, green)
    for yy in range(13, 39, 9):
        _draw_line(img, w, h, x0, yy * scale, x1, yy * scale,
                   1.2 * scale, green)

    return _downsample_rgba(img, w, h, scale)


def make_kitten_peek() -> tuple[bytearray, int, int]:
    """Recreate the peeking kitten motif without sampling the mockup."""
    scale = 2
    w, h = 74 * scale, 56 * scale
    img = _canvas(w, h)
    pink = _rgba(0xFF69AA)
    cream = _rgba(0xFFF1F7)
    blush = _rgba(0xFFB1CC)
    dark = _rgba(0x5A3041)

    _fill_polygon(img, w, h,
                  [(12 * scale, 38 * scale), (20 * scale, 8 * scale),
                   (34 * scale, 31 * scale)], pink)
    _fill_polygon(img, w, h,
                  [(40 * scale, 31 * scale), (54 * scale, 8 * scale),
                   (62 * scale, 38 * scale)], pink)
    _fill_polygon(img, w, h,
                  [(17 * scale, 34 * scale), (21 * scale, 15 * scale),
                   (31 * scale, 31 * scale)], blush)
    _fill_polygon(img, w, h,
                  [(43 * scale, 31 * scale), (53 * scale, 15 * scale),
                   (57 * scale, 34 * scale)], blush)
    _fill_ellipse(img, w, h, 37 * scale, 42 * scale,
                  27 * scale, 23 * scale, pink)
    _fill_ellipse(img, w, h, 37 * scale, 43 * scale,
                  23 * scale, 20 * scale, cream)
    _fill_circle(img, w, h, 29 * scale, 40 * scale, 2.5 * scale, dark)
    _fill_circle(img, w, h, 45 * scale, 40 * scale, 2.5 * scale, dark)
    _fill_polygon(img, w, h,
                  [(37 * scale, 45 * scale), (33 * scale, 48 * scale),
                   (41 * scale, 48 * scale)], blush)
    _draw_line(img, w, h, 37 * scale, 48 * scale,
               37 * scale, 52 * scale, 1.3 * scale, dark)
    return _downsample_rgba(img, w, h, scale)


def make_kitten_sleep() -> tuple[bytearray, int, int]:
    """Recreate the curled sleeping kitten from clean primitives."""
    scale = 2
    w, h = 118 * scale, 74 * scale
    img = _canvas(w, h)
    pink = _rgba(0xFF69AA)
    cream = _rgba(0xFFF1F7)
    blush = _rgba(0xFFB1CC)
    dark = _rgba(0x5A3041)

    _fill_ellipse(img, w, h, 57 * scale, 45 * scale,
                  48 * scale, 23 * scale, pink)
    _fill_ellipse(img, w, h, 59 * scale, 44 * scale,
                  43 * scale, 19 * scale, cream)
    _fill_circle(img, w, h, 35 * scale, 32 * scale, 19 * scale, pink)
    _fill_circle(img, w, h, 35 * scale, 34 * scale, 16 * scale, cream)
    _fill_polygon(img, w, h,
                  [(21 * scale, 26 * scale), (25 * scale, 9 * scale),
                   (34 * scale, 23 * scale)], blush)
    _fill_polygon(img, w, h,
                  [(37 * scale, 23 * scale), (46 * scale, 9 * scale),
                   (49 * scale, 28 * scale)], blush)
    _draw_line(img, w, h, 27 * scale, 35 * scale,
               32 * scale, 37 * scale, 1.5 * scale, dark)
    _draw_line(img, w, h, 39 * scale, 37 * scale,
               44 * scale, 35 * scale, 1.5 * scale, dark)
    _draw_line(img, w, h, 92 * scale, 45 * scale,
               106 * scale, 31 * scale, 6 * scale, pink)
    _draw_line(img, w, h, 106 * scale, 31 * scale,
               111 * scale, 42 * scale, 5 * scale, pink)
    return _downsample_rgba(img, w, h, scale)


def make_kitten_paws() -> tuple[bytearray, int, int]:
    scale = 2
    w, h = 60 * scale, 68 * scale
    img = _canvas(w, h)
    pink = _rgba(0xFF69AA, 200)

    def paw(cx: float, cy: float) -> None:
        _fill_ellipse(img, w, h, cx, cy, 7 * scale, 6 * scale, pink)
        for ox, oy in ((-8, -7), (0, -10), (8, -7)):
            _fill_circle(img, w, h,
                         cx + ox * scale, cy + oy * scale,
                         3.2 * scale, pink)

    paw(19 * scale, 25 * scale)
    paw(40 * scale, 49 * scale)
    return _downsample_rgba(img, w, h, scale)


def make_magic_castle() -> tuple[bytearray, int, int]:
    """Rebuild the blue/gold castle skyline from clean primitives."""
    scale = 2
    w, h = 230 * scale, 78 * scale
    img = _canvas(w, h)
    blue = _rgba(0x0D3266, 210)
    gold = _rgba(0xE2B860, 235)
    glow = _rgba(0x2FA8FF, 210)
    ground = 64 * scale
    towers = [(18, 42, 34), (58, 30, 28), (102, 18, 34),
              (150, 34, 28), (190, 44, 26)]

    for x0, top, width in towers:
        x = x0 * scale
        y = top * scale
        tw = width * scale
        _fill_rect(img, w, h, x, y, x + tw, ground, blue)
        _fill_polygon(img, w, h,
                      [(x - 4 * scale, y),
                       (x + tw / 2, y - 15 * scale),
                       (x + tw + 4 * scale, y)], gold)
        _fill_rect(img, w, h,
                   int(x + tw * 0.42), int(y + 12 * scale),
                   int(x + tw * 0.58), int(y + 24 * scale), glow)

    _fill_rect(img, w, h, 12 * scale, ground, 218 * scale, 70 * scale, blue)
    for x in (28, 76, 118, 164, 204):
        _fill_circle(img, w, h, x * scale, 59 * scale, 3 * scale, gold)
    _fill_polygon(img, w, h,
                  _star_points(205 * scale, 16 * scale,
                               7 * scale, 3 * scale, 5), gold)
    return _downsample_rgba(img, w, h, scale)


def make_magic_lantern() -> tuple[bytearray, int, int]:
    scale = 2
    w, h = 56 * scale, 72 * scale
    img = _canvas(w, h)
    gold = _rgba(0xE2B860)
    blue = _rgba(0x2FA8FF, 215)
    glow = _rgba(0xFFF2C2, 200)

    _draw_line(img, w, h, 18 * scale, 15 * scale,
               28 * scale, 5 * scale, 3 * scale, gold)
    _draw_line(img, w, h, 28 * scale, 5 * scale,
               38 * scale, 15 * scale, 3 * scale, gold)
    _fill_polygon(img, w, h,
                  [(13 * scale, 18 * scale), (43 * scale, 18 * scale),
                   (39 * scale, 58 * scale), (17 * scale, 58 * scale)], gold)
    _fill_polygon(img, w, h,
                  [(18 * scale, 23 * scale), (38 * scale, 23 * scale),
                   (35 * scale, 52 * scale), (21 * scale, 52 * scale)], blue)
    _fill_ellipse(img, w, h, 28 * scale, 39 * scale,
                  8 * scale, 14 * scale, glow)
    _fill_rect(img, w, h, 20 * scale, 58 * scale,
               36 * scale, 64 * scale, gold)
    return _downsample_rgba(img, w, h, scale)


def make_magic_owl() -> tuple[bytearray, int, int]:
    scale = 2
    w, h = 62 * scale, 78 * scale
    img = _canvas(w, h)
    navy = _rgba(0x0D3266)
    gold = _rgba(0xE2B860)
    cream = _rgba(0xFFF2C2)
    blue = _rgba(0x2FA8FF)

    _fill_polygon(img, w, h,
                  [(11 * scale, 24 * scale), (17 * scale, 6 * scale),
                   (27 * scale, 20 * scale)], gold)
    _fill_polygon(img, w, h,
                  [(35 * scale, 20 * scale), (45 * scale, 6 * scale),
                   (51 * scale, 24 * scale)], gold)
    _fill_ellipse(img, w, h, 31 * scale, 45 * scale,
                  25 * scale, 30 * scale, navy)
    _fill_ellipse(img, w, h, 31 * scale, 33 * scale,
                  22 * scale, 17 * scale, gold)
    _fill_circle(img, w, h, 23 * scale, 33 * scale, 7 * scale, cream)
    _fill_circle(img, w, h, 39 * scale, 33 * scale, 7 * scale, cream)
    _fill_circle(img, w, h, 23 * scale, 33 * scale, 3 * scale, navy)
    _fill_circle(img, w, h, 39 * scale, 33 * scale, 3 * scale, navy)
    _fill_polygon(img, w, h,
                  [(31 * scale, 37 * scale), (26 * scale, 43 * scale),
                   (36 * scale, 43 * scale)], blue)
    _fill_polygon(img, w, h,
                  [(12 * scale, 43 * scale), (2 * scale, 56 * scale),
                   (17 * scale, 62 * scale)], gold)
    _fill_polygon(img, w, h,
                  [(50 * scale, 43 * scale), (60 * scale, 56 * scale),
                   (45 * scale, 62 * scale)], gold)
    return _downsample_rgba(img, w, h, scale)


def make_magic_quill() -> tuple[bytearray, int, int]:
    scale = 2
    w, h = 36 * scale, 70 * scale
    img = _canvas(w, h)
    gold = _rgba(0xE2B860)
    cream = _rgba(0xFFF2C2)
    blue = _rgba(0x2FA8FF)

    _draw_line(img, w, h, 8 * scale, 63 * scale,
               28 * scale, 8 * scale, 2.5 * scale, gold)
    feather = [(28, 7), (34, 17), (29, 30), (23, 43), (16, 55),
               (9, 61), (12, 48), (11, 34), (17, 20)]
    _fill_polygon(img, w, h,
                  [(x * scale, y * scale) for x, y in feather], cream)
    _draw_line(img, w, h, 12 * scale, 55 * scale,
               29 * scale, 11 * scale, 1.5 * scale, blue)
    for x1, y1, x2, y2 in ((16, 48, 8, 44), (19, 40, 11, 35),
                           (22, 32, 14, 27), (24, 24, 31, 20),
                           (20, 38, 29, 34)):
        _draw_line(img, w, h,
                   x1 * scale, y1 * scale, x2 * scale, y2 * scale,
                   1 * scale, gold)
    return _downsample_rgba(img, w, h, scale)


def make_magic_corner() -> tuple[bytearray, int, int]:
    scale = 2
    w, h = 36 * scale, 36 * scale
    img = _canvas(w, h)
    gold = _rgba(0xE2B860, 225)
    blue = _rgba(0x2FA8FF, 180)

    _draw_line(img, w, h, 2 * scale, 30 * scale,
               2 * scale, 6 * scale, 2 * scale, gold)
    _draw_line(img, w, h, 2 * scale, 6 * scale,
               30 * scale, 6 * scale, 2 * scale, gold)
    _draw_line(img, w, h, 7 * scale, 30 * scale,
               7 * scale, 12 * scale, 1 * scale, blue)
    _draw_line(img, w, h, 7 * scale, 12 * scale,
               28 * scale, 12 * scale, 1 * scale, blue)
    _fill_polygon(img, w, h,
                  _star_points(29 * scale, 6 * scale,
                               4 * scale, 1.8 * scale, 5), gold)
    _fill_circle(img, w, h, 7 * scale, 29 * scale, 3 * scale, gold)
    return _downsample_rgba(img, w, h, scale)


def make_soccer_field() -> tuple[bytearray, int, int]:
    """Create an 800x408 transparent soccer-pitch background overlay."""
    w, h = 800, 408
    img = _canvas(w, h)
    stripe_a = _rgba(0x0A3A20, 56)
    stripe_b = _rgba(0x0D4727, 82)
    white = _rgba(0xEFFFF2, 70)
    spot = _rgba(0xEFFFF2, 95)

    for i, x in enumerate(range(0, w, 80)):
        _fill_rect(img, w, h, x, 0, min(w, x + 80), h,
                   stripe_a if i % 2 == 0 else stripe_b)

    inset = 18
    _draw_line(img, w, h, inset, inset, w - inset, inset, 2, white)
    _draw_line(img, w, h, w - inset, inset,
               w - inset, h - inset, 2, white)
    _draw_line(img, w, h, w - inset, h - inset,
               inset, h - inset, 2, white)
    _draw_line(img, w, h, inset, h - inset, inset, inset, 2, white)
    _draw_line(img, w, h, w / 2, inset, w / 2, h - inset, 2, white)

    _draw_circle_outline(img, w, h, w / 2, h / 2, 60, 2, white)
    _fill_circle(img, w, h, w / 2, h / 2, 3, spot)

    for left in (True, False):
        if left:
            goal_x = inset
            penalty_x = 132
            six_x = 70
            penalty_spot = 92
        else:
            goal_x = w - inset
            penalty_x = w - 132
            six_x = w - 70
            penalty_spot = w - 92

        y1, y2 = 102, h - 102
        sy1, sy2 = 148, h - 148
        _draw_line(img, w, h, penalty_x, y1, goal_x, y1, 2, white)
        _draw_line(img, w, h, penalty_x, y2, goal_x, y2, 2, white)
        _draw_line(img, w, h, penalty_x, y1, penalty_x, y2, 2, white)
        _draw_line(img, w, h, six_x, sy1, goal_x, sy1, 2, white)
        _draw_line(img, w, h, six_x, sy2, goal_x, sy2, 2, white)
        _draw_line(img, w, h, six_x, sy1, six_x, sy2, 2, white)
        _fill_circle(img, w, h, penalty_spot, h / 2, 2.5, spot)

    return img, w, h


def rotate_rgba(src: bytes, w: int, h: int, turns: int) -> tuple[bytearray, int, int]:
    turns %= 4
    if turns == 0:
        return bytearray(src), w, h

    if turns in (1, 3):
        nw, nh = h, w
    else:
        nw, nh = w, h
    out = bytearray(nw * nh * 4)

    for y in range(h):
        for x in range(w):
            if turns == 1:
                nx, ny = h - 1 - y, x
            elif turns == 2:
                nx, ny = w - 1 - x, h - 1 - y
            else:
                nx, ny = y, w - 1 - x
            src_i = (y * w + x) * 4
            dst_i = (ny * nw + nx) * 4
            out[dst_i:dst_i + 4] = src[src_i:src_i + 4]

    return out, nw, nh


def rgba_to_lvgl_bgra(rgba: bytes) -> bytes:
    """LVGL lv_color32_t memory order is blue, green, red, alpha."""
    out = bytearray(len(rgba))
    for i in range(0, len(rgba), 4):
        out[i] = rgba[i + 2]
        out[i + 1] = rgba[i + 1]
        out[i + 2] = rgba[i]
        out[i + 3] = rgba[i + 3]
    return bytes(out)


def c_array(data: bytes, cols: int = 16) -> str:
    rows = []
    for i in range(0, len(data), cols):
        rows.append("    " + ", ".join(f"0x{x:02X}" for x in data[i:i + cols]) + ",")
    return "\n".join(rows)


def descriptor(name: str, rgba: bytes, w: int, h: int) -> str:
    data = rgba_to_lvgl_bgra(rgba)
    sym = f"ART_{name}"
    return f"""
static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t {sym}_map[] = {{
{c_array(data)}
}};

static const lv_image_dsc_t {sym} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .flags = 0,
        .w = {w},
        .h = {h},
        .stride = {w * 4},
        .reserved_2 = 0,
    }},
    .data_size = sizeof({sym}_map),
    .data = {sym}_map,
    .reserved = NULL,
    .reserved_2 = NULL,
}};
"""


def signature() -> str:
    h = hashlib.sha256(GENERATOR_VERSION.encode())
    h.update(Path(__file__).read_bytes())
    for path in THEMES.values():
        h.update(path.read_bytes())
    return h.hexdigest()

def generate() -> None:
    sig = signature()
    if OUT.is_file():
        first = OUT.read_text(encoding="utf-8", errors="ignore").splitlines()[:1]
        if first and first[0] == f"/* theme-art-signature:{sig} */":
            print("[theme-art] generated sprites are current")
            return

    images: dict[str, tuple[bytearray, int, int]] = {
        "HEADER_CLASSIC": make_header_icon("classic"),
        "HEADER_SOCCER": make_header_icon("soccer"),
        "HEADER_KITTENS": make_header_icon("kittens"),
        "HEADER_MAGIC": make_header_icon("magic"),
        "SOCCER_FIELD": make_soccer_field(),
        "KITTEN_PEEK": make_kitten_peek(),
        "KITTEN_SLEEP": make_kitten_sleep(),
        "KITTEN_PAWS": make_kitten_paws(),
        "FAN_CLASSIC": make_fan_icon("classic"),
        "FAN_SOCCER": make_fan_icon("soccer"),
        "FAN_KITTENS": make_fan_icon("kittens"),
        "FAN_MAGIC": make_fan_icon("magic"),
        "SOCCER_GOAL": make_soccer_goal(),
        "MAGIC_CASTLE": make_magic_castle(),
        "MAGIC_LANTERN": make_magic_lantern(),
        "MAGIC_OWL": make_magic_owl(),
        "MAGIC_QUILL": make_magic_quill(),
        "MAGIC_CORNER_TL": make_magic_corner(),
    }

    corner, cw, ch = images["MAGIC_CORNER_TL"]
    images["MAGIC_CORNER_TR"] = rotate_rgba(corner, cw, ch, 1)
    images["MAGIC_CORNER_BR"] = rotate_rgba(corner, cw, ch, 2)
    images["MAGIC_CORNER_BL"] = rotate_rgba(corner, cw, ch, 3)

    order = [
        "HEADER_CLASSIC", "HEADER_SOCCER", "HEADER_KITTENS", "HEADER_MAGIC",
        "SOCCER_FIELD",
        "KITTEN_PEEK", "KITTEN_SLEEP", "KITTEN_PAWS",
        "FAN_CLASSIC", "FAN_SOCCER", "FAN_KITTENS", "FAN_MAGIC",
        "SOCCER_GOAL",
        "MAGIC_CASTLE", "MAGIC_LANTERN",
        "MAGIC_OWL", "MAGIC_QUILL",
        "MAGIC_CORNER_TL", "MAGIC_CORNER_TR",
        "MAGIC_CORNER_BL", "MAGIC_CORNER_BR",
    ]

    parts = [
        f"/* theme-art-signature:{sig} */",
        "/* AUTO-GENERATED by tools/generate_theme_art.py. DO NOT EDIT. */",
        "#ifndef LV_ATTRIBUTE_MEM_ALIGN\n#define LV_ATTRIBUTE_MEM_ALIGN\n#endif",
        "",
    ]
    for name in order:
        data, w, h = images[name]
        parts.append(descriptor(name, data, w, h))

    parts.append("const lv_image_dsc_t *theme_art_get(theme_art_id_t id)\n{\n    switch (id) {")
    for name in order:
        parts.append(f"    case THEME_ART_{name}: return &ART_{name};")
    parts.append("    default: return NULL;\n    }\n}\n")

    OUT.write_text("\n".join(parts), encoding="utf-8")
    print(f"[theme-art] generated {OUT.relative_to(ROOT)} with {len(order)} transparent sprites")


generate()
