#!/usr/bin/env python3
"""Generate transparent LVGL theme sprites from the canonical mockup SVGs.

The approved theme SVGs are four embedded 400x240 WebP tiles. This script
decodes those tiles through FFmpeg using temporary files (avoiding stdin/WebP
probing problems), reconstructs the 800x480 mockup, crops only decorative art,
removes the local card/screen background, and writes ARGB8888 LVGL descriptors.

The generated include is intentionally not tracked. Normal firmware builds do
not need an image decoder at runtime.
"""

from __future__ import annotations

import base64
import hashlib
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

try:
    Import("env")  # type: ignore[name-defined]
    ROOT = Path(env.subst("$PROJECT_DIR")).resolve()
except NameError:
    ROOT = Path(__file__).resolve().parents[1]

OUT = ROOT / "main" / "theme_art_generated.inc"
WIDTH = 800
HEIGHT = 480
GENERATOR_VERSION = "theme-art-v9-clean-fan-sprites"

THEMES = {
    "kittens": ROOT / "assets" / "kittens-pink" / "screen.svg",
    "soccer": ROOT / "assets" / "soccer-green" / "screen.svg",
    "magic": ROOT / "assets" / "magic-bricks-blue" / "screen.svg",
}

# name, theme, x, y, width, height, bg-removal low/high thresholds
SPRITES = [
    # Crops deliberately contain decorative artwork only. The previous broad
    # crops included source-mockup labels/controls which then appeared offset
    # underneath the live LVGL controls.
    ("KITTEN_PEEK", "kittens", 18, 60, 74, 56, 14, 52),
    ("KITTEN_SLEEP", "kittens", 255, 320, 118, 74, 12, 48),
    ("KITTEN_PAWS", "kittens", 18, 398, 60, 68, 12, 45),

    # Do not crop the source mockups' top-right settings/gear area. Header
    # decoration in that region is rendered natively instead.

    ("MAGIC_CASTLE", "magic", 400, 0, 230, 78, 9, 36),
    ("MAGIC_LANTERN", "magic", 426, 92, 56, 72, 10, 38),
    ("MAGIC_OWL", "magic", 442, 262, 62, 78, 11, 42),
    ("MAGIC_QUILL", "magic", 764, 344, 36, 70, 10, 38),
    ("MAGIC_CORNER_TL", "magic", 14, 80, 36, 36, 8, 30),
]



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


def ensure_ffmpeg() -> str:
    system = shutil.which("ffmpeg")
    if system:
        print(f"[theme-art] using system FFmpeg: {system}")
        return system

    try:
        import imageio_ffmpeg
    except ImportError:
        print("[theme-art] installing imageio-ffmpeg for embedded WebP decoding")
        subprocess.check_call([
            sys.executable, "-m", "pip", "install",
            "--disable-pip-version-check", "imageio-ffmpeg==0.5.1",
        ])
        import imageio_ffmpeg

    exe = imageio_ffmpeg.get_ffmpeg_exe()
    if not exe or not Path(exe).is_file():
        raise RuntimeError("Unable to locate a usable FFmpeg executable")
    print(f"[theme-art] using bundled FFmpeg: {exe}")
    return exe


def numeric_attr(node: ET.Element, name: str, default: int) -> int:
    raw = node.attrib.get(name)
    if raw is None:
        return default
    match = re.match(r"\s*([-+]?[0-9]*\.?[0-9]+)", raw)
    if not match:
        raise ValueError(f"Unsupported SVG {name}: {raw!r}")
    return int(round(float(match.group(1))))


def data_href(node: ET.Element) -> str | None:
    for key, value in node.attrib.items():
        if key == "href" or key.endswith("}href"):
            return value
    return None


def decode_tile(ffmpeg: str, raw: bytes, mime: str, width: int, height: int) -> bytes:
    suffix = {
        "webp": ".webp",
        "png": ".png",
        "jpeg": ".jpg",
        "jpg": ".jpg",
    }.get(mime.lower())
    if not suffix:
        raise ValueError(f"Unsupported embedded image type image/{mime}")

    path = None
    try:
        with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as tmp:
            tmp.write(raw)
            path = tmp.name

        proc = subprocess.run([
            ffmpeg, "-hide_banner", "-loglevel", "error",
            "-i", path,
            "-frames:v", "1",
            "-vf", f"scale={width}:{height}:flags=lanczos",
            "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1",
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)

        expected = width * height * 4
        if proc.returncode != 0 or len(proc.stdout) != expected:
            detail = proc.stderr.decode("utf-8", errors="replace").strip()
            raise RuntimeError(
                f"FFmpeg failed to decode {width}x{height} artwork tile: {detail}"
            )
        return proc.stdout
    finally:
        if path:
            try:
                os.unlink(path)
            except OSError:
                pass


def render_svg(path: Path, ffmpeg: str) -> bytearray:
    root = ET.fromstring(path.read_text(encoding="utf-8"))
    canvas = bytearray((0, 0, 0, 255)) * (WIDTH * HEIGHT)

    for node in root.iter():
        if node.tag.split("}")[-1] != "image":
            continue
        href = data_href(node)
        if not href:
            continue
        m = re.match(r"^data:image/([^;]+);base64,(.*)$", href, re.DOTALL)
        if not m:
            continue

        x = numeric_attr(node, "x", 0)
        y = numeric_attr(node, "y", 0)
        w = numeric_attr(node, "width", 0)
        h = numeric_attr(node, "height", 0)
        tile = decode_tile(ffmpeg, base64.b64decode(m.group(2)), m.group(1), w, h)

        for row in range(h):
            src = row * w * 4
            dst = ((y + row) * WIDTH + x) * 4
            canvas[dst:dst + w * 4] = tile[src:src + w * 4]

    return canvas


def crop_rgba(src: bytes, x0: int, y0: int, w: int, h: int) -> bytearray:
    out = bytearray(w * h * 4)
    for y in range(h):
        src_i = ((y0 + y) * WIDTH + x0) * 4
        dst_i = y * w * 4
        out[dst_i:dst_i + w * 4] = src[src_i:src_i + w * 4]
    return out


def median(values: list[int]) -> int:
    values = sorted(values)
    return values[len(values) // 2]


def remove_background(rgba: bytearray, w: int, h: int, low: int, high: int) -> bytearray:
    """Turn the local mockup background transparent while retaining artwork.

    The median perimeter color is a stable estimate for each crop's card/screen
    background. Pixel distance from that color becomes a feathered alpha mask.
    """
    border = []
    for x in range(w):
        border.extend((x, (h - 1) * w + x))
    for y in range(1, h - 1):
        border.extend((y * w, y * w + w - 1))

    rs = [rgba[i * 4] for i in border]
    gs = [rgba[i * 4 + 1] for i in border]
    bs = [rgba[i * 4 + 2] for i in border]
    bg = (median(rs), median(gs), median(bs))

    out = bytearray(len(rgba))
    span = max(1, high - low)

    for i in range(w * h):
        p = i * 4
        r, g, b = rgba[p], rgba[p + 1], rgba[p + 2]
        d = math.sqrt(
            (r - bg[0]) * (r - bg[0]) +
            (g - bg[1]) * (g - bg[1]) +
            (b - bg[2]) * (b - bg[2])
        )

        if d <= low:
            a = 0
        elif d >= high:
            a = 255
        else:
            t = (d - low) / span
            a = int(255 * t * t * (3.0 - 2.0 * t))

        # Preserve RGB for straight-alpha blending.
        out[p:p + 4] = bytes((r, g, b, a))

    return out


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
    for path in THEMES.values():
        h.update(path.read_bytes())
    for spec in SPRITES:
        h.update(repr(spec).encode())
    return h.hexdigest()


def generate() -> None:
    sig = signature()
    if OUT.is_file():
        first = OUT.read_text(encoding="utf-8", errors="ignore").splitlines()[:1]
        if first and first[0] == f"/* theme-art-signature:{sig} */":
            print("[theme-art] generated sprites are current")
            return

    ffmpeg = ensure_ffmpeg()
    screens = {name: render_svg(path, ffmpeg) for name, path in THEMES.items()}

    images: dict[str, tuple[bytearray, int, int]] = {
        "HEADER_CLASSIC": make_header_icon("classic"),
        "HEADER_SOCCER": make_header_icon("soccer"),
        "HEADER_KITTENS": make_header_icon("kittens"),
        "HEADER_MAGIC": make_header_icon("magic"),
        "FAN_CLASSIC": make_fan_icon("classic"),
        "FAN_SOCCER": make_fan_icon("soccer"),
        "FAN_KITTENS": make_fan_icon("kittens"),
        "FAN_MAGIC": make_fan_icon("magic"),
        "SOCCER_GOAL": make_soccer_goal(),
    }
    for name, theme, x, y, w, h, low, high in SPRITES:
        # The canonical mockups contain the obsolete configuration gear in the
        # top-right header. Reject any generated sprite that would sample that
        # region so it cannot accidentally reappear in a future theme edit.
        if y < 80 and (x + w) > 700:
            raise ValueError(
                f"{name} overlaps the forbidden top-right settings-gear region"
            )

        crop = crop_rgba(screens[theme], x, y, w, h)
        images[name] = (remove_background(crop, w, h, low, high), w, h)
        print(f"[theme-art] extracted {name} from {theme} at {x},{y} {w}x{h}")

    corner, cw, ch = images["MAGIC_CORNER_TL"]
    images["MAGIC_CORNER_TR"] = rotate_rgba(corner, cw, ch, 1)
    images["MAGIC_CORNER_BR"] = rotate_rgba(corner, cw, ch, 2)
    images["MAGIC_CORNER_BL"] = rotate_rgba(corner, cw, ch, 3)

    order = [
        "HEADER_CLASSIC", "HEADER_SOCCER", "HEADER_KITTENS", "HEADER_MAGIC",
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
