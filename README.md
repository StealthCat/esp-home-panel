# ESP Home Panel — Waveshare 7C BOX

Four themed ESPHome/LVGL dashboards for the Waveshare **ESP32-S3-Touch-LCD-7C-BOX** (800×480):

| Folder | Theme |
|---|---|
| `panels/classic-dark/` | Original dark smart-home dashboard |
| `panels/soccer-green/` | Soccer + green |
| `panels/kittens-pink/` | Kittens + pink |
| `panels/magic-bricks-blue/` | Magical-school + building-bricks + blue |

Each panel controls the same four Home Assistant entities through the ESPHome native API:

- Tapo LED strip — on/off, brightness, and color presets
- Overhead light — on/off and dimming
- Ceiling fan smart switch — on/off
- Amazon Echo Dot — volume, mute, play/pause and an optional announcement event

## Hardware target

The shared hardware package is configured specifically for the **7C BOX**, not the older Waveshare 7-inch board. It uses the 7C's official 800×480 ST7262 RGB timings and GPIO map, GT911 touch on I²C GPIO47/GPIO48 with IRQ GPIO4, and the Waveshare CH32V003 I/O expander for panel power/backlight control.

Use **ESPHome 2026.7.0 or newer** because the native `waveshare_io_ch32v003` component was added in the 2026.7 line.

## Configure a panel

1. Pick one folder under `panels/`.
2. Copy `secrets.example.yaml` to your ESPHome secrets file or provide the same keys globally.
3. Edit the four entity substitutions near the top of that panel's `panel.yaml` to match your Home Assistant entity IDs.
4. In Home Assistant, allow this ESPHome device to perform Home Assistant actions.
5. Validate and install:

```bash
esphome config panels/classic-dark/panel.yaml
esphome run panels/classic-dark/panel.yaml
```

Repeat with the desired theme folder.

## Entity substitutions

Every panel exposes the same substitutions so themes can be swapped without rewriting automations:

```yaml
tapo_led_entity: light.tapo_led_strip
overhead_light_entity: light.overhead_light
ceiling_fan_entity: switch.ceiling_fan
echo_dot_entity: media_player.echo_dot
```

These values are placeholders; change them to the actual entity IDs in your Home Assistant instance.

## Echo announcements

The panel fires the Home Assistant event `esphome_panel_echo_announce` when the **Announce** button is pressed. This keeps the panel independent of whichever Alexa integration/notify service you use. Create one Home Assistant automation that listens for that event and sends the announcement through your preferred Alexa/Echo integration.

## Structure

```text
shared/
  waveshare-7c-hardware.yaml
  home-assistant-controls.yaml
  secrets.example.yaml
panels/
  classic-dark/
  soccer-green/
  kittens-pink/
  magic-bricks-blue/
```

Every theme folder contains its own `panel.yaml`, LVGL UI, SVG artwork, vector preview, and theme notes. The hardware and Home Assistant bridge are shared so behavior remains consistent across all four designs.

## Notes

The artwork in this repository is original. The blue magical theme is intentionally a generic enchanted boarding-school/building-brick aesthetic and does not include third-party franchise logos or copyrighted character art.
