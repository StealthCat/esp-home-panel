# Native ESP-IDF + LVGL firmware

This is the primary Waveshare 7C BOX implementation.

## Toolchain

Use ESP-IDF 5.5.3.

```bash
. $HOME/esp/esp-idf/export.sh
cd idf
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
```

## Hardware

The board support in `main/board_7c.c` follows Waveshare's 7C reference:

- LCD: 800×480 RGB565
- PCLK: 16 MHz
- HSYNC GPIO46
- VSYNC GPIO3
- DE GPIO5
- PCLK GPIO7
- RGB data: GPIO14,38,18,17,10,39,0,45,9,8,21,1,2,42,41,40
- I2C: SDA GPIO47 / SCL GPIO48 at 400 kHz
- GT911 interrupt: GPIO4
- GT911 reset: I/O expander IO1
- LCD backlight enable: I/O expander IO2
- I/O expander address: 0x24

## MQTT protocol

Default base topic: `home/7c-panel`.

Panel subscribes to retained state:

```text
home/7c-panel/state/tapo
home/7c-panel/state/overhead
home/7c-panel/state/fan
home/7c-panel/state/echo
```

and publishes commands to:

```text
home/7c-panel/cmd/tapo
home/7c-panel/cmd/overhead
home/7c-panel/cmd/fan
home/7c-panel/cmd/echo
```

The Home Assistant package in `../home-assistant/` implements this bridge.

## Theme selection

Run `idf.py menuconfig`, open **Home Panel**, and select one of:

- Classic dark
- Soccer green
- Kittens pink
- Magic / bricks blue

All four use the same control layout and state model; only styling and
decorative art change.
