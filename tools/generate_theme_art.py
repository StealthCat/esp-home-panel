#!/usr/bin/env python3
"""Pack high-resolution transparent PNG theme artwork into LVGL ARGB8888 sprites.

The source PNGs in assets/theme-sprites are freshly recreated artwork based on
approved UI mockups. They are committed source assets, not crops taken from the
mockup screenshots. This script decodes them with the Python standard library,
downsamples with alpha-aware averaging, and writes the generated LVGL include.
"""

from __future__ import annotations

import hashlib
import struct
import zlib
from pathlib import Path

try:
    Import("env")  # type: ignore[name-defined]
    ROOT = Path(env.subst("$PROJECT_DIR")).resolve()
except NameError:
    ROOT = Path(__file__).resolve().parents[1]

OUT = ROOT / "main" / "theme_art_generated.inc"
ASSET_ROOT = ROOT / "assets" / "theme-sprites"
GENERATOR_VERSION = "theme-art-v11-hires-png-assets"

ASSETS = {
    "HEADER_CLASSIC": ("header_classic.png", 56, 56),
    "HEADER_SOCCER": ("header_soccer.png", 56, 56),
    "HEADER_KITTENS": ("header_kittens.png", 56, 56),
    "HEADER_MAGIC": ("header_magic.png", 56, 56),
    "SOCCER_FIELD": ("soccer_field.png", 800, 408),
    "KITTEN_PEEK": ("kitten_peek.png", 74, 56),
    "KITTEN_SLEEP": ("kitten_sleep.png", 118, 74),
    "KITTEN_PAWS": ("kitten_paws.png", 60, 68),
    "FAN_CLASSIC": ("fan_classic.png", 142, 116),
    "FAN_SOCCER": ("fan_soccer.png", 142, 116),
    "FAN_KITTENS": ("fan_kittens.png", 142, 116),
    "FAN_MAGIC": ("fan_magic.png", 142, 116),
    "SOCCER_GOAL": ("soccer_goal.png", 76, 44),
    "MAGIC_CASTLE": ("magic_castle.png", 230, 78),
    "MAGIC_LANTERN": ("magic_lantern.png", 56, 72),
    "MAGIC_OWL": ("magic_owl.png", 62, 78),
    "MAGIC_QUILL": ("magic_quill.png", 36, 70),
    "MAGIC_CORNER_TL": ("magic_corner_tl.png", 36, 36),
}

ORDER = [
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


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png_rgba(path: Path) -> tuple[bytearray, int, int]:
    """Decode an 8-bit, non-interlaced RGBA PNG using only stdlib."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")

    width = height = None
    compressed = bytearray()
    pos = 8
    while pos < len(data):
        if pos + 12 > len(data):
            raise ValueError(f"Truncated PNG: {path}")
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        pos += 12 + length

        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filter_method, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if bit_depth != 8 or color_type != 6:
                raise ValueError(
                    f"{path} must be 8-bit RGBA PNG "
                    f"(got depth={bit_depth}, type={color_type})"
                )
            if compression != 0 or filter_method != 0 or interlace != 0:
                raise ValueError(f"{path} uses unsupported PNG encoding")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break

    if width is None or height is None:
        raise ValueError(f"PNG missing IHDR: {path}")

    raw = zlib.decompress(bytes(compressed))
    bpp = 4
    stride = width * bpp
    expected = height * (stride + 1)
    if len(raw) != expected:
        raise ValueError(
            f"Unexpected PNG payload size for {path}: {len(raw)} != {expected}"
        )

    out = bytearray(width * height * bpp)
    prev = bytearray(stride)
    src = 0
    for y in range(height):
        filter_type = raw[src]
        src += 1
        scan = bytearray(raw[src:src + stride])
        src += stride

        for x in range(stride):
            left = scan[x - bpp] if x >= bpp else 0
            up = prev[x]
            up_left = prev[x - bpp] if x >= bpp else 0
            if filter_type == 0:
                pass
            elif filter_type == 1:
                scan[x] = (scan[x] + left) & 0xFF
            elif filter_type == 2:
                scan[x] = (scan[x] + up) & 0xFF
            elif filter_type == 3:
                scan[x] = (scan[x] + ((left + up) >> 1)) & 0xFF
            elif filter_type == 4:
                scan[x] = (scan[x] + _paeth(left, up, up_left)) & 0xFF
            else:
                raise ValueError(
                    f"Unsupported PNG filter {filter_type} in {path}"
                )

        dst = y * stride
        out[dst:dst + stride] = scan
        prev = scan

    return out, width, height


def downsample_rgba(
    src: bytearray, sw: int, sh: int, factor: int
) -> tuple[bytearray, int, int]:
    if factor == 1:
        return bytearray(src), sw, sh
    if sw % factor or sh % factor:
        raise ValueError(
            f"Image {sw}x{sh} is not divisible by scale {factor}"
        )

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
                    a = src[p + 3]
                    alpha_sum += a
                    rgb_weighted[0] += src[p] * a
                    rgb_weighted[1] += src[p + 1] * a
                    rgb_weighted[2] += src[p + 2] * a

            d = (y * dw + x) * 4
            if alpha_sum:
                out[d] = int(round(rgb_weighted[0] / alpha_sum))
                out[d + 1] = int(round(rgb_weighted[1] / alpha_sum))
                out[d + 2] = int(round(rgb_weighted[2] / alpha_sum))
            out[d + 3] = int(round(alpha_sum / samples))

    return out, dw, dh


def load_asset(
    filename: str, target_w: int, target_h: int
) -> tuple[bytearray, int, int]:
    path = ASSET_ROOT / filename
    rgba, sw, sh = decode_png_rgba(path)
    if sw % target_w or sh % target_h:
        raise ValueError(
            f"{filename}: source {sw}x{sh} is not an integer multiple of "
            f"{target_w}x{target_h}"
        )
    fx = sw // target_w
    fy = sh // target_h
    if fx != fy or fx < 1:
        raise ValueError(f"{filename}: non-uniform scale {fx}x{fy}")
    rgba, w, h = downsample_rgba(rgba, sw, sh, fx)
    if (w, h) != (target_w, target_h):
        raise ValueError(
            f"{filename}: decoded to {w}x{h}, "
            f"expected {target_w}x{target_h}"
        )
    print(f"[theme-art] loaded {filename}: {sw}x{sh} -> {w}x{h}")
    return rgba, w, h


def rotate_rgba(
    src: bytes, w: int, h: int, turns: int
) -> tuple[bytearray, int, int]:
    turns %= 4
    if turns == 0:
        return bytearray(src), w, h
    nw, nh = (h, w) if turns in (1, 3) else (w, h)
    out = bytearray(nw * nh * 4)
    for y in range(h):
        for x in range(w):
            if turns == 1:
                nx, ny = h - 1 - y, x
            elif turns == 2:
                nx, ny = w - 1 - x, h - 1 - y
            else:
                nx, ny = y, w - 1 - x
            si = (y * w + x) * 4
            di = (ny * nw + nx) * 4
            out[di:di + 4] = src[si:si + 4]
    return out, nw, nh


def rgba_to_lvgl_bgra(rgba: bytes) -> bytes:
    out = bytearray(len(rgba))
    for i in range(0, len(rgba), 4):
        out[i] = rgba[i + 2]
        out[i + 1] = rgba[i + 1]
        out[i + 2] = rgba[i]
        out[i + 3] = rgba[i + 3]
    return bytes(out)


def c_array(data: bytes, cols: int = 16) -> str:
    return "\n".join(
        "    " + ", ".join(f"0x{x:02X}" for x in data[i:i + cols]) + ","
        for i in range(0, len(data), cols)
    )


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
    h.update((ROOT / "tools" / "generate_theme_art.py").read_bytes())
    for name in sorted(ASSETS):
        h.update(name.encode())
        h.update((ASSET_ROOT / ASSETS[name][0]).read_bytes())
    return h.hexdigest()


def generate() -> None:
    sig = signature()
    if OUT.is_file():
        first = OUT.read_text(
            encoding="utf-8", errors="ignore"
        ).splitlines()[:1]
        if first and first[0] == f"/* theme-art-signature:{sig} */":
            print("[theme-art] generated sprites are current")
            return

    images = {
        name: load_asset(*spec) for name, spec in ASSETS.items()
    }

    corner, cw, ch = images["MAGIC_CORNER_TL"]
    images["MAGIC_CORNER_TR"] = rotate_rgba(corner, cw, ch, 1)
    images["MAGIC_CORNER_BR"] = rotate_rgba(corner, cw, ch, 2)
    images["MAGIC_CORNER_BL"] = rotate_rgba(corner, cw, ch, 3)

    parts = [
        f"/* theme-art-signature:{sig} */",
        "/* AUTO-GENERATED by tools/generate_theme_art.py. DO NOT EDIT. */",
        "#ifndef LV_ATTRIBUTE_MEM_ALIGN\n#define LV_ATTRIBUTE_MEM_ALIGN\n#endif",
        "",
    ]
    for name in ORDER:
        data, w, h = images[name]
        parts.append(descriptor(name, data, w, h))

    parts.append(
        "const lv_image_dsc_t *theme_art_get(theme_art_id_t id)\n"
        "{\n"
        "    switch (id) {"
    )
    for name in ORDER:
        parts.append(
            f"    case THEME_ART_{name}: return &ART_{name};"
        )
    parts.append("    default: return NULL;\n    }\n}\n")

    OUT.write_text("\n".join(parts), encoding="utf-8")
    print(
        f"[theme-art] generated {OUT.relative_to(ROOT)} "
        f"with {len(ORDER)} transparent sprites"
    )


generate()
