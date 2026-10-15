# README screenshots

These captures use the actual UI with deterministic host-fixture data, not live devices or accounts. Panel captures are 800 × 480 pixels. Browser captures use a 1280-pixel desktop viewport.

## Panel UI

From the repository root, install a C/C++ compiler, CMake, Git and Python with Pillow, then run:

```sh
python tools/generate_theme_art.py
git clone --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl.git /tmp/panel-lvgl
cmake -S tests/host -B build/host -DLVGL_DIR=/tmp/panel-lvgl -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel 4
mkdir -p build/previews
build/host/panel_render 0 build/previews/controls-classic.ppm
build/host/panel_render 1 build/previews/controls-soccer.ppm
build/host/panel_render 2 build/previews/controls-kittens.ppm
build/host/panel_render 3 build/previews/controls-magic.ppm
build/host/panel_render 0 build/previews/weather-now.ppm weather
build/host/panel_render 0 build/previews/weather-outlook.ppm forecast
python - <<'PY'
from pathlib import Path
from PIL import Image
for source in Path('build/previews').glob('*.ppm'):
    Image.open(source).save(Path('docs/screenshots') / (source.stem + '.png'), optimize=True)
PY
```

CMake fetches cJSON v1.7.19 unless `CJSON_DIR` points to a local checkout. The renderer includes the production UI and exercises its controls with sample state. This verifies rendering and host behavior, not physical display/touch timing or live service integration.

## Browser settings

Use the same Playwright version as the firmware workflow:

```sh
npm install --no-save --package-lock=false playwright@1.51.1
npx playwright install --with-deps chromium --only-shell
node tools/test_settings_ui.js
cp /tmp/panel-settings-check/display-1280.png docs/screenshots/settings-display.png
cp /tmp/panel-settings-check/system-1280.png docs/screenshots/settings-system.png
```

The browser checks render HTML from `main/web_config.c` through `tools/render_web_config.py`, use fake network/settings data, and exercise desktop/mobile settings. The screenshot fixtures never contact a physical panel. Do not substitute captures containing real credentials or private network information.
