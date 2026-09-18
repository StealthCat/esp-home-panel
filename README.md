# ESP Home Panel — Waveshare 7C BOX

This repository now contains a **native ESP-IDF + LVGL implementation** for the
Waveshare **ESP32-S3-Touch-LCD-7C-BOX**. The custom firmware is the primary
implementation because it gives much tighter control over the visual design,
animation, touch behavior, and state synchronization than the earlier ESPHome
prototype.

The target hardware is the 800×480 Waveshare 7C BOX with ESP32-S3, 16 MB PSRAM,
RGB LCD, GT911 capacitive touch, and Waveshare's I2C I/O-expander/backlight
controller.

## Primary implementation

The native firmware lives in:

```text
idf/
  CMakeLists.txt
  sdkconfig.defaults
  partitions.csv
  main/
    app_main.c
    board_7c.c
    network.c
    mqtt_bridge.c
    ui.c
    ui_theme.c
    Kconfig.projbuild
    idf_component.yml
```

It implements:

- 800×480 RGB565 display initialization for the 7C BOX
- GT911 capacitive touch
- Waveshare I/O-expander reset and backlight control
- ESP-IDF Wi-Fi + SNTP
- MQTT state/command bridge
- native LVGL 9 interface with gradients, bevels, shadows, glows and animation
- Tapo LED strip power, brightness, RGB color wheel and presets
- overhead-light power, dimmer and 25/50/75/100% presets
- ceiling-fan smart-switch control with animated fan graphic
- Echo Dot volume, mute, play/pause and announcement button
- four compile-time themes:
  - Classic dark
  - Soccer green
  - Kittens pink
  - Magic / bricks blue

The decorative graphics are drawn as native LVGL objects rather than a static
full-screen screenshot. That allows the switches, slider positions, fan,
waveform, status indicators and color selector to reflect live device state.

## Home Assistant integration

The custom firmware uses MQTT as the clean boundary between the display and
Home Assistant.

A ready-to-edit Home Assistant package is included at:

```text
home-assistant/esp-home-panel-package.yaml
```

It translates MQTT commands from the panel into Home Assistant service calls
and publishes retained state for:

```text
light.tapo_led_strip
light.overhead_light
switch.ceiling_fan
media_player.echo_dot
```

Change those four entity IDs if your installation uses different names.

The default topic root is:

```text
home/7c-panel
```

## Build

Waveshare currently recommends ESP-IDF **5.5.3** for the 7C examples, and this
project is built around that release.

```bash
git clone https://github.com/StealthCat/esp-home-panel.git
cd esp-home-panel/idf

idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py -p COM_PORT flash monitor
```

Under **Home Panel** in `menuconfig`, configure:

- Wi-Fi SSID/password
- MQTT broker URI and credentials
- MQTT base topic
- timezone
- preferred visual theme
- display backlight brightness

Managed components pull LVGL, Espressif's LVGL port, and the GT911 touch driver
automatically.

## Theme design

### Classic dark
Dark navy/graphite cards, blue glow, restrained smart-home graphics.

### Soccer green
Green stadium/pitch treatment, field markings, brighter green controls and
soccer decorative elements.

### Kittens pink
Mauve/pink glass cards, paws, cat graphics and soft pink highlights.

### Magic / bricks blue
Deep blue background, gold trim, stars, castle silhouettes and original
building-brick-inspired graphics. It intentionally does not use third-party
franchise logos or character artwork.

## Legacy ESPHome prototype

The original ESPHome/LVGL implementation remains under `panels/` and
`shared/` as a reference/prototype. New development should target `idf/`.

## CI

`.github/workflows/validate.yml` builds the native ESP-IDF firmware for the
ESP32-S3 on pushes and pull requests.
