#!/usr/bin/env python3
"""Generate a native LVGL RGB565 background from the selected theme artwork.

The canonical theme files remain SVGs, but they only contain positioned,
base64-encoded raster tiles. At build time this script decodes those tiles with
FFmpeg, composites them in plain Python, converts the result to the panel's
native RGB565 format, and emits one LVGL lv_image_dsc_t for the selected theme.

Only the selected theme is emitted into main/artwork.c, so unused artwork does
not consume firmware flash. No SVG/ThorVG/WebP decoder is required on-device,
and the build does not depend on Pillow's WebP plugin.
"""

from __future__ import annotations

import base64
import re
import shutil
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


def ensure_ffmpeg() -> str:
    """Return an FFmpeg executable, installing a bundled one if necessary."""

    system_ffmpeg = shutil.which("ffmpeg")
    if system_ffmpeg:
        print(f"[artwork] using system FFmpeg: {system_ffmpeg}")
        return system_ffmpeg

    try:
        import imageio_ffmpeg
    except ImportError:
        print(
            "[artwork] FFmpeg not found; installing imageio-ffmpeg "
            "into the PlatformIO Python environment"
        )
        try:
            subprocess.check_call(
                [
                    sys.executable,
                    "-m",
                    "pip",
                    "install",
                    "--disable-pip-version-check",
                    "imageio-ffmpeg==0.5.1",
                ]
            )
        except subprocess.CalledProcessError as exc:
            raise RuntimeError(
                "Unable to install the bundled FFmpeg decoder required for artwork "
                "conversion. Install it manually with: "
                f"{sys.executable} -m pip install imageio-ffmpeg==0.5.1"
            ) from exc

        import imageio_ffmpeg

    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    if not ffmpeg or not Path(ffmpeg).is_file():
        raise RuntimeError("imageio-ffmpeg did not provide a usable FFmpeg executable")

    print(f"[artwork] using bundled FFmpeg: {ffmpeg}")
    return ffmpeg


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


def decode_tile(ffmpeg: str, raw: bytes, mime: str, width: int, height: int) -> bytes:
    """Decode one embedded raster image to exactly width*height RGBA bytes."""

    demuxers = {
        "webp": "webp_pipe",
        "png": "png_pipe",
        "jpeg": "jpeg_pipe",
        "jpg": "jpeg_pipe",
    }
    demuxer = demuxers.get(mime.lower())
    if not demuxer:
        raise ValueError(f"Unsupported embedded artwork MIME type: image/{mime}")

    cmd = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-f",
        demuxer,
        "-i",
        "pipe:0",
        "-frames:v",
        "1",
        "-vf",
        f"scale={width}:{height}:flags=lanczos",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgba",
        "pipe:1",
    ]

    proc = subprocess.run(
        cmd,
        input=raw,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    expected = width * height * 4
    if proc.returncode != 0:
        detail = proc.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(
            "FFmpeg failed to decode an embedded artwork tile"
            + (f": {detail}" if detail else "")
        )

    if len(proc.stdout) != expected:
        detail = proc.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(
            f"FFmpeg decoded {len(proc.stdout)} bytes for a {width}x{height} RGBA "
            f"tile; expected {expected}"
            + (f". Decoder output: {detail}" if detail else "")
        )

    return proc.stdout


def alpha_composite(canvas: bytearray, tile: bytes, x0: int, y0: int, width: int, height: int) -> None:
    """Composite an RGBA tile onto the RGBA screen canvas."""

    for y in range(height):
        canvas_row = ((y0 + y) * WIDTH + x0) * 4
        tile_row = y * width * 4

        for x in range(width):
            src = tile_row + x * 4
            dst = canvas_row + x * 4
            alpha = tile[src + 3]

            if alpha == 255:
                canvas[dst : dst + 4] = tile[src : src + 4]
                continue

            if alpha == 0:
                continue

            inverse = 255 - alpha
            dst_alpha = canvas[dst + 3]

            canvas[dst] = (tile[src] * alpha + canvas[dst] * inverse + 127) // 255
            canvas[dst + 1] = (
                tile[src + 1] * alpha + canvas[dst + 1] * inverse + 127
            ) // 255
            canvas[dst + 2] = (
                tile[src + 2] * alpha + canvas[dst + 2] * inverse + 127
            ) // 255
            canvas[dst + 3] = alpha + (dst_alpha * inverse + 127) // 255


def render_svg_tiles(path: Path, ffmpeg: str) -> bytearray:
    root = ET.fromstring(path.read_text(encoding="utf-8"))

    svg_width = numeric_attr(root, "width", WIDTH)
    svg_height = numeric_attr(root, "height", HEIGHT)
    if (svg_width, svg_height) != (WIDTH, HEIGHT):
        raise ValueError(
            f"{path} must describe a {WIDTH}x{HEIGHT} screen, "
            f"got {svg_width}x{svg_height}"
        )

    # Start with opaque black. Current approved artwork covers the full screen,
    # but this also gives deterministic pixels if a future asset leaves gaps.
    canvas = bytearray((0, 0, 0, 255)) * (WIDTH * HEIGHT)
    tile_count = 0

    for node in root.iter():
        if node.tag.split("}")[-1] != "image":
            continue

        href = data_href(node)
        if not href or not href.startswith("data:image/"):
            raise ValueError(
                f"{path} contains an image element that is not an embedded data URI"
            )

        match = re.match(r"^data:image/([^;]+);base64,(.*)$", href, re.DOTALL)
        if not match:
            raise ValueError(f"Unsupported embedded image URI in {path}")

        mime = match.group(1).lower()

        x = numeric_attr(node, "x", 0)
        y = numeric_attr(node, "y", 0)
        w = numeric_attr(node, "width", 0)
        h = numeric_attr(node, "height", 0)

        if w <= 0 or h <= 0:
            raise ValueError(f"Invalid embedded image size {w}x{h} in {path}")

        if x < 0 or y < 0 or x + w > WIDTH or y + h > HEIGHT:
            raise ValueError(
                f"Embedded image at {x},{y} size {w}x{h} exceeds {WIDTH}x{HEIGHT}"
            )

        raw = base64.b64decode(match.group(2), validate=False)
        tile = decode_tile(ffmpeg, raw, mime, w, h)
        alpha_composite(canvas, tile, x, y, w, h)
        tile_count += 1

    if tile_count == 0:
        raise ValueError(f"No embedded raster artwork found in {path}")

    print(f"[artwork] composited {tile_count} raster tiles from {path.relative_to(ROOT)}")
    return canvas


def rgb565_le(rgba: bytes) -> bytes:
    out = bytearray((len(rgba) // 4) * 2)

    dst = 0
    for src in range(0, len(rgba), 4):
        r = rgba[src]
        g = rgba[src + 1]
        b = rgba[src + 2]
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

    ffmpeg = ensure_ffmpeg()
    rgba = render_svg_tiles(source, ffmpeg)
    data = rgb565_le(rgba)

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
