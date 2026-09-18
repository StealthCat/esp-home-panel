# IDF artwork assets

These are the canonical 800×480 theme artwork sources for the native ESP-IDF/LVGL panel.

Each theme contains a full-screen SVG derived from the corresponding mockup:

- `classic-dark/screen.svg`
- `soccer-green/screen.svg`
- `kittens-pink/screen.svg`
- `magic-bricks-blue/screen.svg`

The SVG files intentionally live under the native `idf/` project now instead of only under the legacy ESPHome prototype.

The current native UI still draws its live controls as LVGL objects so switch positions, sliders, status text, the fan animation, and Echo state can remain dynamic. The SVGs are the visual source-of-truth for bevels, decorative art, card chrome, backgrounds, and spacing. They can also be rendered by LVGL 9's SVG/vector support as background/decorative layers once the static and dynamic elements are split into separate assets.

Do not edit the legacy `panels/*/assets/screen.svg` copies and expect the native IDF build to change. New artwork work should target this directory.
