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
GENERATOR_VERSION = "theme-art-v5-align"

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
    ("KITTEN_PEEK", "kittens", 15, 55, 78, 62, 14, 52),
    ("KITTEN_SLEEP", "kittens", 245, 315, 135, 82, 12, 48),
    ("KITTEN_PAWS", "kittens", 18, 395, 62, 72, 12, 45),
    ("KITTEN_FAN", "kittens", 52, 334, 124, 132, 10, 42),

    # The soccer header ball is partly covered by the original settings gear,
    # so keep the clean fan/goal artwork and draw the header treatment natively.
    ("SOCCER_BALL", "soccer", 744, 0, 56, 70, 12, 44),
    ("SOCCER_GOAL", "soccer", 650, 140, 146, 88, 7, 30),
    ("SOCCER_FAN", "soccer", 52, 334, 124, 132, 8, 34),

    ("MAGIC_CASTLE", "magic", 400, 0, 230, 78, 9, 36),
    ("MAGIC_LANTERN", "magic", 424, 90, 60, 76, 10, 38),
    ("MAGIC_FAN", "magic", 94, 320, 186, 150, 8, 32),
    ("MAGIC_OWL", "magic", 435, 258, 72, 88, 11, 42),
    ("MAGIC_QUILL", "magic", 752, 338, 48, 82, 10, 38),
    ("MAGIC_CORNER_TL", "magic", 14, 80, 36, 36, 8, 30),
]


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

    images: dict[str, tuple[bytearray, int, int]] = {}
    for name, theme, x, y, w, h, low, high in SPRITES:
        crop = crop_rgba(screens[theme], x, y, w, h)
        images[name] = (remove_background(crop, w, h, low, high), w, h)
        print(f"[theme-art] extracted {name} from {theme} at {x},{y} {w}x{h}")

    corner, cw, ch = images["MAGIC_CORNER_TL"]
    images["MAGIC_CORNER_TR"] = rotate_rgba(corner, cw, ch, 1)
    images["MAGIC_CORNER_BR"] = rotate_rgba(corner, cw, ch, 2)
    images["MAGIC_CORNER_BL"] = rotate_rgba(corner, cw, ch, 3)

    order = [
        "KITTEN_PEEK", "KITTEN_SLEEP", "KITTEN_PAWS", "KITTEN_FAN",
        "SOCCER_BALL", "SOCCER_GOAL", "SOCCER_FAN",
        "MAGIC_CASTLE", "MAGIC_LANTERN", "MAGIC_FAN",
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
