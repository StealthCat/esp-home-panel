# Third-party notices

The project's GPL-3.0-only declaration does not replace licenses or notices attached to third-party material.

## Bundled font

**Outfit SemiBold** is distributed under the SIL Open Font License 1.1. Preserve the full copyright and license notice in [assets/fonts/OFL-Outfit.txt](assets/fonts/OFL-Outfit.txt). This applies to the source font and derived font data in `main/font_outfit_semibold_48.c`. Conversion instructions and upstream attribution are in [assets/fonts/README.md](assets/fonts/README.md).

## Build dependencies

Dependencies are resolved by PlatformIO and the ESP-IDF Component Manager rather than vendored as project source. Preserve the license and notice files shipped with the exact versions included in any binary distribution. The dependency declarations are in [platformio.ini](platformio.ini) and [main/idf_component.yml](main/idf_component.yml).

- [ESP-IDF](https://github.com/espressif/esp-idf): Apache-2.0 at the framework level; bundled components can have separate terms.
- [LVGL](https://github.com/lvgl/lvgl): MIT.
- [Espressif mDNS and LVGL port](https://github.com/espressif/esp-protocols) and [LCD touch drivers](https://github.com/espressif/esp-bsp): retain their component license files and notices.
- [cJSON](https://github.com/DaveGamble/cJSON): MIT; used by ESP-IDF and the host renderer.

The Wokwi simulator uses the [Wokwi custom chips API](https://docs.wokwi.com/chips-api/getting-started). Simulator service and SDK terms are separate from this project's firmware license.

## Artwork and external services

Canonical theme PNGs, source hashes and processing information are recorded in [assets/theme-sprites/manifest.json](assets/theme-sprites/manifest.json). Preserve that provenance when redistributing or modifying the artwork.

Home Assistant, Alexa, Tapo, Waveshare and weather-provider names identify compatible hardware or services; they do not imply affiliation or endorsement. Weather data and API access remain subject to their providers' terms. The weather UI displays provider attribution; see [weather setup](docs/weather-display.md) for provider requirements.
