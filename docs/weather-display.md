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

## Direct weather setup

Weather works independently of Home Assistant and MQTT. Open **Settings → Weather**,
choose a station provider, enter its credentials, enable the weather page, and save.
The form shows the credentials needed for the selected providers: Ambient adds
an application key; WU forecasts add a required key for Ambient stations or an
optional separate key for WU stations. Open-Meteo needs no forecast key. Hidden
fields keep their saved values. Changing the station provider requires entering
its API key. Turning Weather off disables configuration without erasing it.

Keys are stored on the panel. Saved keys are never filled back into the HTML form;
leave a key blank to keep it or use its Clear checkbox to remove it.

| Provider | Station field | Credentials |
| --- | --- | --- |
| Weather Underground | PWS station ID, for example `KNYNEWYO123` | WU API key |
| Ambient Weather | Exact station name (case insensitive), or station MAC address | Personal API key **and** application key |

For WU, use your account's **My Profile → My Devices** to find the station ID and
**Member Settings → API Keys** to find your key. For Ambient Weather, open your
[AmbientWeather.net account](https://ambientweather.net/account) to generate API
and application keys; station names/MAC addresses are in your device settings.
Duplicate Ambient station names require the MAC address so selection is unambiguous.
A WU display name is not its station ID. Credentials for the two services are not interchangeable.

### Forecast source

- **Open-Meteo (default):** the same approach as Heat Index Display. Uses the
  station's latitude/longitude for five daily forecasts and the next twelve hours.
  No extra key is required. The free endpoint is for noncommercial use; see
  [Open-Meteo terms](https://open-meteo.com/en/terms). Source attribution is on screen.
- **Weather Underground:** daily five-day forecast using the WU key. Your key must
  include the Weather Company v3 daily forecast service; observation access alone
  does not guarantee this. You can supply a separate forecast key, required when
  observations come from Ambient. The UI shows an explicit error if access is
  denied. There is no silent provider fallback. This option has no hourly chart.
- **Disabled:** station observations only.

Forecasts require valid coordinates in the station response. Missing coordinates
are reported on screen. Forecast times, sunrise and sunset use the panel's
configured POSIX timezone, with sunrise/sunset displayed in 12-hour am/pm format; set that to the station's timezone under Display.
Select imperial or metric display units independently of the provider.

### Navigation and information

Swipe horizontally or tap the title/time header to switch between Home Control
and Weather. The header label simply reads **Controls** or **Weather**. Three weather tabs provide:

- **Now:** outdoor temperature, calculated feels-like temperature, humidity,
  dew point, wind speed/gusts and compass, rain total/rate, UV/solar radiation,
  pressure, heat index, sunrise/sunset, and tomorrow's temperatures.
- **Outlook:** twelve-hour temperature line and rain-probability bars with
  time labels, plus five daily cards with high/low, illustrated conditions, and
  rain chance. Tap a day for precipitation, wind, UV, sunrise/sunset, and the
  provider's narrative when supplied. Daily icons describe the forecast,
  not an observation of current sky conditions.
- **Station:** a vertically scrolling grid of available readings, including
  Ambient indoor, extra temperature/humidity, soil, air-quality, rain-period,
  battery and lightning channels. Up to 48 fields; a notice identifies truncation.
  Missing values show `--` and are never substituted with zero.

All views inherit Classic, Soccer, Kittens, or Magic colors, typography, artwork,
and card decorations. Weather icons, the compass and chart are drawn natively
by LVGL. Touch controls below the header retain their gestures, and the selected
page remains selected after sleep.

### Refresh, outages and upgrades

Observations refresh every 1–30 minutes (default one); forecasts every 15 minutes.
HTTPS verifies provider certificates and waits for the network clock to sync.
The separate worker keeps network requests off the touch/UI task. Responses are
bounded to 96 KiB. Failed requests back off to 15 minutes; rate-limit Retry-After
values are honored up to one hour. API keys are URL-encoded and HTTP client URL
logging is suppressed.

The last valid readings stay visible during outages with a **Cached** error
notice. Actual station observation timestamps determine age; observations older
than 15 minutes or delivery gaps beyond the configured interval plus two minutes
are marked stale. Forecasts older than 30 minutes are marked stale. Caches are
in RAM and reset on reboot. No live credentials are included in the repository.

Upgrading from the previous `weather` branch requires configuring the new Weather
tab. Old HA weather entity settings are ignored. Remove the obsolete HA weather
package if installed; the control bridge package is still needed for lights,
fan and media controls. Sensor MQTT discovery is unchanged.

## Verification

- `python tools/test_weather.py`: actual provider parsers, station matching,
  missing/null/zero values, units, forecast schemas, URL encoding and backoff.
- `python tools/test_display_policy.py`: sleep, touch override and lux fallback.
- `python tools/test_color_settings.py`: validation, persistence and blank-key semantics.
- `node tools/test_settings_ui.js`: desktop/mobile weather tab and form submission.
- Host LVGL renderer: all four themes and weather views, control gestures and
  wake-only first touch. GitHub Actions retains firmware and UI preview artifacts.

Live provider credentials and physical sensor/backlight behavior still need
on-device verification after installing the firmware.
