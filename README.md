# ESP Home Panel — Waveshare 7C BOX

Four high-fidelity ESPHome/LVGL dashboards for the Waveshare **ESP32-S3-Touch-LCD-7C-BOX** (800×480), rebuilt so the live display closely follows the original visual mockups instead of approximating them with flat LVGL cards.

| Folder | Theme |
|---|---|
| `panels/classic-dark/` | Original dark/navy smart-home dashboard |
| `panels/soccer-green/` | Soccer + green |
| `panels/kittens-pink/` | Kittens + pink |
| `panels/magic-bricks-blue/` | Magical-school + building-bricks + blue |

Each theme controls the same four Home Assistant entities through the ESPHome native API:

- Tapo LED strip — on/off, brightness and six color presets
- Overhead light — on/off, continuous dimming and 25/50/75/100% presets
- Ceiling fan smart switch — on/off from the fan graphic or toggle area
- Amazon Echo Dot — volume, mute, play/pause and an optional announcement event

## Mockup-faithful rendering

Each panel now uses a complete **800×480 `assets/screen.svg`** as the visible UI. ESPHome converts that artwork to RGB565 at compile time, preserving the mockup's beveled cards, inner highlights, shadows, gradients, artwork, field textures, kittens, castle/brick graphics and decorative icons.

`shared/ui-dashboard.yaml` then places **transparent LVGL hit targets and sliders** over the visible buttons, toggles and slider tracks. The result keeps the rich mockup appearance while retaining real touchscreen control.

This also means the individual old `header-art.svg`, `fan.svg`, `echo.svg` and simplified `preview.svg` assets are no longer used; the complete screen artwork is the single visual source of truth for each theme.

## Hardware target

The shared hardware package is configured specifically for the **7C BOX**, not the older Waveshare 7-inch board. It uses the 7C's 800×480 RGB panel configuration, GT911 touch controller and Waveshare CH32V003 I/O expander for panel/backlight control.

Use **ESPHome 2026.7.0 or newer** because the native `waveshare_io_ch32v003` component is required.

## Configure a panel

1. Pick one folder under `panels/`.
2. Copy `shared/secrets.example.yaml` values into your ESPHome secrets file or supply the same keys globally.
3. Edit the four entity substitutions near the top of that panel's `panel.yaml` to match your Home Assistant entity IDs.
4. In Home Assistant, allow this ESPHome device to perform Home Assistant actions.
5. Validate and install, for example:

```bash
esphome config panels/classic-dark/panel.yaml
esphome run panels/classic-dark/panel.yaml
```

Repeat with whichever theme you want to flash.

## Automated validation

`.github/workflows/validate.yml` runs `esphome config` against **all four panels** using ESPHome 2026.8.2 on every push to `main` and on pull requests. CI-only dummy secrets are created inside each panel directory during the workflow and are never committed.

## Entity substitutions

Every panel uses the same substitutions:

```yaml
tapo_led_entity: light.tapo_led_strip
overhead_light_entity: light.overhead_light
ceiling_fan_entity: switch.ceiling_fan
echo_dot_entity: media_player.echo_dot
```

Change these placeholders to the actual entity IDs in your Home Assistant instance.

## Echo announcements

The **Announce** touch region fires the Home Assistant event `esphome.panel_echo_announce` with `entity_id` and `panel` event data. Create a Home Assistant automation that listens for that event and routes it through whichever Alexa/Echo notify integration you use.

## Repository structure

```text
.github/workflows/
  validate.yml
shared/
  waveshare-7c-hardware.yaml
  home-assistant-controls.yaml
  ui-dashboard.yaml
  secrets.example.yaml
panels/
  classic-dark/
    panel.yaml
    assets/screen.svg
  soccer-green/
    panel.yaml
    assets/screen.svg
  kittens-pink/
    panel.yaml
    assets/screen.svg
  magic-bricks-blue/
    panel.yaml
    assets/screen.svg
```

The blue magical theme is intentionally an original generic enchanted boarding-school/building-brick aesthetic and does not contain third-party franchise logos or copyrighted character art.
