# Wokwi emulator

This simulation runs the same ESP-IDF/LVGL application as the physical Waveshare
7C BOX build. Only the LCD/touch transport is replaced when the `wokwi`
PlatformIO environment defines `WOKWI_EMULATOR`.

## Build and run

```bash
pio run -e wokwi
wokwi-cli .
```

You can also open the repository in VS Code with the Wokwi extension, build the
`wokwi` environment, and choose **Wokwi: Start Simulator**.

The Wokwi PlatformIO environment also creates a merged flash image at
`.pio/build/wokwi/wokwi.bin`. This includes the bootloader, partition table,
and application in one image and avoids relative-path problems with PlatformIO
ESP-IDF `flasher_args.json` files.

The virtual panel is an 800×480 Wokwi custom framebuffer chip. LVGL sends RGB565
flush regions to it over simulated SPI:

- SCK: GPIO12
- MOSI: GPIO11
- CS: GPIO10

Touch is exposed through a small simulated I2C device at address `0x38`:

- SCL: GPIO8
- SDA: GPIO9

Wokwi's Custom Chips framebuffer API currently does not expose mouse/touch
coordinates from direct clicks on a custom framebuffer. Use the custom-chip
controls **Touch X**, **Touch Y**, and **Touch pressed** to inject touch input.

The normal `waveshare-7c-box` PlatformIO environment is unchanged and still
uses the real RGB panel, GT911, and Waveshare I/O expander.

## Rebuild the custom chip

The compiled WebAssembly chip is committed so the emulator can start without a
separate chip-build step. If you modify its C source, rebuild it with:

```bash
wokwi-cli chip compile wokwi/7cbox-display.chip.c \
  -o wokwi/7cbox-display.chip.wasm
```
