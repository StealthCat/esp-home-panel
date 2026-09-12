# ESP Home Panel — Waveshare 7C BOX

Four themed ESPHome/LVGL dashboards for the Waveshare **ESP32-S3-Touch-LCD-7C-BOX** (800×480).

| Folder | Theme |
|---|---|
| `panels/classic-dark/` | Original dark smart-home dashboard |
| `panels/soccer-green/` | Soccer + green |
| `panels/kittens-pink/` | Kittens + pink |
| `panels/magic-bricks-blue/` | Magical-school + building-bricks + blue |

Each theme includes a complete `panel.yaml`, live SVG artwork, and a vector screen preview. The shared 800×480 LVGL control surface keeps behavior identical while each folder supplies its own colors and artwork.

## Controls

- **Tapo LED strip** — power, brightness, six color presets (red/orange/yellow/green/blue/purple)
- **Overhead light smart dimmer** — power, brightness slider, 25/50/75/100% presets
- **Ceiling fan smart switch** — state display and toggle
- **Amazon Echo Dot** — live state, volume, mute/unmute, play/pause and announcement event

## Hardware target

The shared hardware package is configured specifically for the **7C BOX**, not the older Waveshare 7-inch board. It uses the 7C's official 800×480 ST7262 RGB timings and GPIO map, GT911 touch on I²C GPIO47/GPIO48 with IRQ GPIO4, and the Waveshare CH32V003 I/O expander for display reset/power/backlight.

Use **ESPHome 2026.7.0 or newer** because the native `waveshare_io_ch32v003` component is required.

## Configure a panel

1. Copy the values from `shared/secrets.example.yaml` into your ESPHome `secrets.yaml` and replace every placeholder.
2. Pick one theme under `panels/`.
3. Edit the four entity substitutions near the top of that theme's `panel.yaml`.
4. In Home Assistant, open the ESPHome integration for the panel and enable **Allow the device to perform Home Assistant actions**.
5. Validate/install from the repository root:

```bash
esphome config panels/classic-dark/panel.yaml
esphome run panels/classic-dark/panel.yaml
```

## Entity substitutions

```yaml
tapo_led_entity: light.tapo_led_strip
overhead_light_entity: light.overhead_light
ceiling_fan_entity: switch.ceiling_fan
echo_dot_entity: media_player.echo_dot
```

These are placeholders. Replace them with the actual entity IDs in your Home Assistant instance.

## Echo announcements

The **Announce** button fires `esphome.panel_echo_announce` with `entity_id` and `panel` event data. Create a Home Assistant automation that listens for the event and routes it to the Alexa/Echo integration or notify action you use. Keeping this as an event avoids hard-coding a third-party Alexa integration into the panel firmware.

## Repository layout

```text
shared/
  waveshare-7c-hardware.yaml
  home-assistant-controls.yaml
  ui-dashboard.yaml
  secrets.example.yaml
panels/
  classic-dark/
    panel.yaml
    preview.svg
    assets/
  soccer-green/
  kittens-pink/
  magic-bricks-blue/
```

## Artwork

All embedded SVG artwork is original and theme-specific. The blue theme uses a generic enchanted boarding-school/building-brick aesthetic and intentionally contains no franchise logos or character artwork.
