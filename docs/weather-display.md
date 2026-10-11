# Presence wake, automatic brightness, and weather

These features are optional and initially disabled. Existing saved configurations
keep their existing display behavior. Save settings and allow the panel to restart.

## Display tab

- **Turn off when unoccupied:** uses the local C4001 reading; Home Assistant and
  MQTT do not need to be connected. Enable the C4001 under Sensors first.
- **Absence delay:** 5–240 seconds, default 60, after the radar reports vacant.
  This adds to the radar's own configured vacancy delay.
- **Stay awake after touch:** 1–30 minutes, default 2. Any interaction extends
  this period. Presence keeps the display awake. The first touch on a dark
  display only wakes it, including when that touch lands on a light switch.
- A disconnected, restarting, or stale radar keeps the display awake. Only the
  backlight switches off: touch, sensors, networking, and OTA continue running.
- **Automatic brightness:** requires the VEML7700 enabled under Sensors and the
  **7C-BOX PWM backlight**. A smoothed logarithmic curve maps darkness to the
  minimum and 1,000 lux or more to the maximum; changes ramp gradually.
- Minimum/maximum defaults are 10/100%, configurable from 5–100%. Unavailable,
  saturated, or stale lux readings fall back to the manual backlight setting.
- **The original ESP32-S3-Touch-LCD-7 supports on/off only.** Its backlight stays
  on while awake; automatic brightness has no dimming effect on that hardware.
  No software darkening overlay or hardware modification is included.
- A tap can recover even a manually zeroed backlight. Place the lux sensor away
  from direct screen light to avoid feedback into automatic brightness.

## Home Assistant setup

1. Keep the existing `esp-home-panel-mqtt-bridge.yaml` package installed.
2. Copy **`home-assistant/esp-home-panel-weather.yaml`** into your HA packages
   directory alongside it. If using a custom MQTT base prefix, change the
   wildcard trigger topic in the new package to match. Reload automations or
   restart Home Assistant.
3. Under the panel's **Home Assistant → Weather** settings, enable Weather page.
4. Set **Station outdoor temperature entity** to your station's actual outdoor
   temperature sensor, e.g. `sensor.backyard_temperature`. This reading becomes
   the large temperature summary. The package includes this sensor first and
   the other `sensor.*` entities belonging to the same Home Assistant device.
   Entity names are not assumed or hardcoded. A standalone sensor without a
   device supplies only itself. Sensors attached to other HA devices are not
   implicitly combined.
5. Set **Daily forecast entity** to a `weather.*` entity whose integration
   supports daily `weather.get_forecasts`. Ambient station measurements and
   forecast data are separate sources. HA keeps any provider credentials.
6. Save and restart the panel. Swipe left/right anywhere across the title/time
   header to switch pages, or tap that area. Controls below the header retain
   their existing gestures. The current page remains selected after sleep.

Station readings use their HA names and units, including temperature, humidity,
wind, pressure, rain totals, UV, solar radiation, and any extra sensor entities
that the station device exposes. The detail grid scrolls vertically. Long labels
and values scroll within their cells. Unknown/unavailable values are explicitly
labelled and are never converted to zero. Up to 48 station sensors are supported;
a visible notice identifies truncation for larger devices.

Five forecast cards show today and the following days when provided (at most five
available daily entries), with high/low temperatures, condition icons, and rain
probability or precipitation amount. Missing fields use `--`; a provider without
daily forecasts shows an unavailable message. Dates follow Home Assistant's local
timezone, while the panel clock uses its configured POSIX timezone.

The weather page inherits Classic, Soccer, Kittens, and Magic theme colors,
fonts, borders, existing background artwork, and decorative treatment.

## MQTT contract and freshness

Topics are relative to `<base>/<panel-id>`:

| Topic | Direction | Content |
| --- | --- | --- |
| `request_weather` | Panel → HA | `station`, `forecast`, `refresh_forecast` |
| `state/weather` | HA → Panel | Version 1 station snapshot |
| `state/forecast` | HA → Panel | Version 1 daily forecast snapshot |

Station requests run every 30 seconds, forecasts every 15 minutes. Connection
and HA birth trigger an immediate refresh of both. Replies are QoS 1 and **not
retained**, avoiding an old forecast appearing fresh after a panel restart.

A station snapshot includes `version: 1`, `available`, `name`, `age_seconds`,
`truncated`, and `fields: [{label, value, unit}]`; all field text is sent as strings.
The package derives observation age from the selected temperature entity's HA
`last_reported` timestamp. The page marks station observations older than 15
minutes or delivery gaps over 2 minutes as stale. This age relies on the HA
integration reporting station updates accurately.

A forecast snapshot includes `version: 1`, `available`, and
`days: [{date, condition, high, low, rain}]`. Forecast delivery older than 30
minutes is marked stale. Previously received readings remain visible with that
status during a broker/HA outage. Failed forecast retrieval is shown as unavailable.

The receiver reassembles sequential MQTT fragments, caps payloads at 24 KiB,
validates the version/schema, bounds every text field, and publishes coherent
snapshots to the LVGL task. Oversize/malformed messages are discarded.

## Verification

- `python tools/test_display_policy.py`: vacancy deadlines, touch override,
  radar failure, brightness limits, invalid lux, and manual fallback.
- `python tools/test_weather.py`: production parser and fragment assembly,
  unavailable values, limits, and real HA payload templates (requires Jinja2
  and PyYAML; cJSON is taken from the installed SDK or pinned upstream v1.7.19).
- `python tools/test_color_settings.py`: saved settings, migration, validation,
  reset, and legacy forms.
- `node tools/test_settings_ui.js`: desktop/mobile settings and submitted values.
- Host LVGL renderer: all four themes, real pointer gestures, wake-only first
  touch, subsequent control interaction, weather layout, and stale indicators.
- GitHub Actions on `weather`: firmware build and downloadable firmware plus
  settings/physical-display preview artifacts.

Physical C4001/VEML7700/backlight behavior and a live HA station still require
on-device verification after installing the firmware and package.
