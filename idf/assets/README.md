# IDF artwork assets

These are the canonical **800×480 mockup-faithful display assets** used by the native ESP-IDF/LVGL firmware.

## Themes

- `classic-dark/screen.svg`
- `soccer-green/screen.svg`
- `kittens-pink/screen.svg`
- `magic-bricks-blue/screen.svg`

The classic, soccer, and kitten SVGs are now self-contained: their approved mockup raster tiles are inlined directly as data URIs rather than referenced as external files. The magic/brick theme remains a self-contained vector composition.

The native build generates/embeds the selected artwork through `main/artwork.c` and LVGL renders it as the bottom display layer. Interactive LVGL widgets then sit above it.

## Artwork-first rendering

The goal is to preserve the mockups' visual details directly:

- card bevels and glass highlights
- exact panel spacing
- themed background textures
- soccer pitch/goal/stadium imagery
- kitten illustrations, paw prints, hearts and handwritten accents
- magical castle, moon, stars, gold trim and brick-inspired decoration
- original icon placement and dashboard composition

LVGL still owns the stateful portions of the UI: switches, sliders, RGB selection, touch targets, fan state and media commands. Static duplicate LVGL labels/cards are suppressed while artwork mode is active so they do not cover the mockup graphics.

The `tiles/` directories are retained as source/reference pieces for the raster-backed themes, but `screen.svg` no longer depends on those files at runtime.

## Editing

Treat `screen.svg` as the source of truth for visual fidelity. After changing an SVG, regenerate `main/artwork.c` from the SVG content before building so the firmware copy remains synchronized.
