# Display artwork

These 25 RGBA PNGs are isolated from the supplied September 25 sprite sheets.
`manifest.json` records each source filename, SHA-256, crop rectangle, extraction
steps and native display footprint. Images retain transparent edges and their
original proportions. Source canvases are twice the runtime footprint; the
stdlib-only packer averages premultiplied color/alpha before storing LVGL BGRA
pixels. The display itself remains RGB565.

The PNGs are the canonical build inputs. No artwork generator, network access,
Pillow or source sheet is needed to build firmware. Do not recreate them from
geometric substitutes. The illustrated sheets' labels, status-report text and
unused header banners are not display assets.

`tools/generate_theme_art.py` packs these PNGs into the ignored
`main/theme_art_generated.inc`. Three rotated magic corner variants are derived
from `magic_corner_tl.png`; they do not require duplicate source files.
`tools/check_theme_assets.py` rejects unused/missing PNGs and IDs, invisible
images and invalid sizes. It also runs the packer.

All 28 packed images are used by `main/ui.c`. The unused quill, old header
images, full-screen SVG mockups, obsolete raster fallback and procedural asset
builders have been removed. CI validates and renders committed artwork; it has
read-only permissions and cannot overwrite the source images.
