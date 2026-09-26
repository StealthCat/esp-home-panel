#!/usr/bin/env python3
"""Ensure critical ESP-IDF memory settings are present in PlatformIO's active sdkconfig.

PlatformIO keeps a generated sdkconfig.<environment> in the project root. Kconfig
defaults only seed that file; once it exists, later changes to sdkconfig.defaults
can be masked by stale values. This pre-build script makes the few settings that
are required for the ESP32-S3 + PSRAM memory layout deterministic.
"""

from pathlib import Path

Import("env")  # PlatformIO/SCons

ROOT = Path(env.subst("$PROJECT_DIR")).resolve()
PIOENV = env.subst("$PIOENV")
SDKCONFIG = ROOT / f"sdkconfig.{PIOENV}"

REQUIRED = {
    "CONFIG_SPIRAM": "y",
    "CONFIG_SPIRAM_USE_MALLOC": "y",
    "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY": "y",
    "CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP": "y",
    "CONFIG_LV_USE_CLIB_MALLOC": "y",
    # Theme sprites are embedded as LV_COLOR_FORMAT_ARGB8888.  LVGL's
    # ESP-IDF/Kconfig integration turns support for a source format off when
    # the corresponding CONFIG_ symbol is absent (or stale sdkconfig files say
    # it is disabled), even though the image descriptor itself is valid.
    "CONFIG_LV_USE_DRAW_SW": "y",
    "CONFIG_LV_DRAW_SW_SUPPORT_RGB565": "y",
    "CONFIG_LV_DRAW_SW_SUPPORT_ARGB8888": "y",
    "CONFIG_LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED": "y",
    "CONFIG_COMPILER_OPTIMIZATION_SIZE": "y",
    "CONFIG_LV_FONT_MONTSERRAT_36": "y",
}

DISABLED = {
    "CONFIG_LV_FONT_MONTSERRAT_48",
    "CONFIG_LV_USE_BUILTIN_MALLOC",
    "CONFIG_COMPILER_OPTIMIZATION_PERF",
    "CONFIG_ESP_WIFI_IRAM_OPT",
    "CONFIG_ESP_WIFI_RX_IRAM_OPT",
    "CONFIG_ESP_WIFI_EXTRA_IRAM_OPT",
    "CONFIG_ESP_WIFI_SLP_IRAM_OPT",
    "CONFIG_LV_USE_VECTOR_GRAPHIC",
    "CONFIG_LV_USE_THORVG",
    "CONFIG_LV_USE_THORVG_INTERNAL",
    "CONFIG_LV_USE_SVG",
}


def key_of(line: str):
    stripped = line.strip()
    if stripped.startswith("CONFIG_") and "=" in stripped:
        return stripped.split("=", 1)[0]
    if stripped.startswith("# CONFIG_") and stripped.endswith(" is not set"):
        return stripped[2:].split(" ", 1)[0]
    return None


def update_sdkconfig() -> None:
    if SDKCONFIG.exists():
        lines = SDKCONFIG.read_text(encoding="utf-8").splitlines()
    else:
        lines = []

    managed = set(REQUIRED) | DISABLED
    output = []
    seen = set()

    for line in lines:
        key = key_of(line)
        if key not in managed:
            output.append(line)
            continue

        if key in seen:
            continue
        seen.add(key)

        if key in REQUIRED:
            output.append(f"{key}={REQUIRED[key]}")
        else:
            output.append(f"# {key} is not set")

    for key, value in REQUIRED.items():
        if key not in seen:
            output.append(f"{key}={value}")

    for key in DISABLED:
        if key not in seen:
            output.append(f"# {key} is not set")

    SDKCONFIG.write_text("\n".join(output) + "\n", encoding="utf-8")
    print(f"[sdkconfig] enforced PSRAM/LVGL memory settings in {SDKCONFIG.name}")


update_sdkconfig()

