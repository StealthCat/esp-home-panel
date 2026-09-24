#!/usr/bin/env python3
"""Generate a native LVGL RGB565 background from the selected theme artwork.

The canonical theme files remain SVGs, but they only contain positioned,
base64-encoded raster tiles. At build time this script composites those tiles
once, converts the result to the panel's native RGB565 format, and emits a
single LVGL lv_image_dsc_t for the compile-time selected theme.

Only the selected theme is emitted into main/artwork.c, so unused artwork does
not consume firmware flash. No SVG/ThorVG/WebP decoder is required on-device.
"""

from __future__ import annotations

import base64
import io
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

try:
    Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.
    ROOT = Path(env.subst("$PROJECT_DIR")).resolve()
    PIOENV = env.subst("$PIOENV")
except NameError:
    ROOT = Path(__file__).resolve().parents[1]
    PIOENV = "waveshare-7c-box"

MAIN = ROOT / "main"
WIDTH = 800
HEIGHT = 480
STRIDE = WIDTH * 2

THEMES = [
    (
        "CONFIG_PANEL_THEME_CLASSIC",
        "CLASSIC_IMG",
        ROOT / "assets" / "classic-dark" / "screen.svg",
    ),
    (
        "CONFIG_PANEL_THEME_SOCCER",
        "SOCCER_IMG",
        ROOT / "assets" / "soccer-green" / "screen.svg",
    ),
    (
        "CONFIG_PANEL_THEME_KITTENS",
        "KITTENS_IMG",
        ROOT / "assets" / "kittens-pink" / "screen.svg",
    ),
    (
        "CONFIG_PANEL_THEME_MAGIC",
        "MAGIC_IMG",
        ROOT / "assets" / "magic-bricks-blue" / "screen.svg",
    ),
]


def ensure_pillow():
    try:
        from PIL import Image
        return Image
    except ImportError:
        print("[artwork] Pillow not found; installing Pillow==11.3.0 into the PlatformIO Python environment")
        try:
            subprocess.check_call(
                [
                    sys.executable,
                    "-m",
                    "pip",
                    "install",
                    "--disable-pip-version-check",
                    "Pillow==11.3.0",
                ]
            )
        except subprocess.CalledProcessError as exc:
            raise RuntimeError(
                "Unable to install Pillow, which is required to convert the embedded "
                "WebP artwork into native RGB565. Install it manually with: "
                f"{sys.executable} -m pip install Pillow==11.3.0"
            ) from exc

        from PIL import Image
        return Image


def selected_theme():
    config_paths = [
        ROOT / f"sdkconfig.{PIOENV}",
        ROOT / "sdkconfig",
        ROOT / "sdkconfig.defaults",
    ]

    config_text = ""
    for path in config_paths:
        if path.is_file():
            config_text += "\n" + path.read_text(encoding="utf-8", errors="ignore")

    for config, symbol, path in THEMES:
        if re.search(rf"(?m)^\s*{re.escape(config)}=y\s*$", config_text):
            return config, symbol, path

    # Kconfig's PANEL_THEME choice defaults to Classic.
    return THEMES[0]


def numeric_attr(node: ET.Element, name: str, default: int) -> int:
    raw = node.attrib.get(name)
    if raw is None:
        return default

    match = re.match(r"\s*([-+]?[0-9]*\.?[0-9]+)", raw)
    if not match:
        raise ValueError(f"Unsupported SVG {name} value: {raw!r}")
    return int(round(float(match.group(1))))


def data_href(node: ET.Element) -> str | None:
    for key, value in node.attrib.items():
        if key == "href" or key.endswith("}href"):
            return value
    return None


def render_svg_tiles(path: Path, Image):
    root = ET.fromstring(path.read_text(encoding="utf-8"))

    svg_width = numeric_attr(root, "width", WIDTH)
    svg_height = numeric_attr(root, "height", HEIGHT)
    if (svg_width, svg_height) != (WIDTH, HEIGHT):
        raise ValueError(
            f"{path} must describe a {WIDTH}x{HEIGHT} screen, "
            f"got {svg_width}x{svg_height}"
        )

    canvas = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 255))
    tile_count = 0

    for node in root.iter():
        if node.tag.split("}")[-1] != "image":
            continue

        href = data_href(node)
        if not href or not href.startswith("data:image/"):
            raise ValueError(
                f"{path} contains an image element that is not an embedded data URI"
            )

        match = re.match(r"^data:image/[^;]+;base64,(.*)$", href, re.DOTALL)
        if not match:
            raise ValueError(f"Unsupported embedded image URI in {path}")

        raw = base64.b64decode(match.group(1), validate=False)
        tile = Image.open(io.BytesIO(raw)).convert("RGBA")

        x = numeric_attr(node, "x", 0)
        y = numeric_attr(node, "y", 0)
        w = numeric_attr(node, "width", tile.width)
        h = numeric_attr(node, "height", tile.height)

        if w <= 0 or h <= 0:
            raise ValueError(f"Invalid embedded image size {w}x{h} in {path}")

        if tile.size != (w, h):
            tile = tile.resize((w, h), Image.Resampling.LANCZOS)

        if x < 0 or y < 0 or x + w > WIDTH or y + h > HEIGHT:
            raise ValueError(
                f"Embedded image at {x},{y} size {w}x{h} exceeds {WIDTH}x{HEIGHT}"
            )

        canvas.alpha_composite(tile, (x, y))
        tile_count += 1

    if tile_count == 0:
        raise ValueError(f"No embedded raster artwork found in {path}")

    print(f"[artwork] composited {tile_count} raster tiles from {path.relative_to(ROOT)}")
    return canvas.convert("RGB")


def rgb565_le(image) -> bytes:
    rgb = image.tobytes()
    out = bytearray((len(rgb) // 3) * 2)

    dst = 0
    for src in range(0, len(rgb), 3):
        r = rgb[src]
        g = rgb[src + 1]
        b = rgb[src + 2]
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

        # ESP32-S3 is little-endian and LVGL's native RGB565 buffer uses host
        # byte order when swap_bytes is disabled.
        out[dst] = value & 0xFF
        out[dst + 1] = (value >> 8) & 0xFF
        dst += 2

    return bytes(out)


def c_array(data: bytes, columns: int = 16) -> str:
    lines = []
    for offset in range(0, len(data), columns):
        row = data[offset : offset + columns]
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in row) + ",")
    return "\n".join(lines)


def generate() -> None:
    config, symbol, source = selected_theme()
    if not source.is_file():
        raise FileNotFoundError(f"Missing artwork asset: {source}")

    Image = ensure_pillow()
    image = render_svg_tiles(source, Image)
    data = rgb565_le(image)

    expected_size = WIDTH * HEIGHT * 2
    if len(data) != expected_size:
        raise RuntimeError(
            f"Generated RGB565 artwork is {len(data)} bytes; expected {expected_size}"
        )

    output = f"""/* AUTO-GENERATED by tools/generate_artwork.py. DO NOT EDIT. */
#include "artwork.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_ARTWORK
#define LV_ATTRIBUTE_ARTWORK
#endif

static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_ARTWORK
uint8_t {symbol}_map[] = {{
{c_array(data)}
}};

static const lv_image_dsc_t {symbol} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = {WIDTH},
        .h = {HEIGHT},
        .stride = {STRIDE},
        .reserved_2 = 0,
    }},
    .data_size = sizeof({symbol}_map),
    .data = {symbol}_map,
    .reserved = NULL,
    .reserved_2 = NULL,
}};

const lv_image_dsc_t *artwork_get_active_image(void)
{{
    return &{symbol};
}}
"""

    output_path = MAIN / "artwork.c"
    output_path.write_text(output, encoding="utf-8")
    print(
        f"[artwork] generated {output_path.relative_to(ROOT)}: "
        f"{config}, {WIDTH}x{HEIGHT} RGB565, {len(data)} bytes"
    )


generate()
