# Sensors

The firmware supports one **VEML7700 ambient light breakout** at fixed I2C address `0x10` and one optional **Gravity C4001 SEN0610 presence sensor** (described below). Both are disabled by default. Other sensor models require their own drivers; enabling these settings does not add sensors inside the panel.

## Wiring

Power off before connecting a 3.3 V compatible breakout. Connect ground, 3.3 V, SDA and SCL to the panel's existing I2C bus. Do not connect 5 V pull-ups to ESP32 GPIOs. Verify connector labels against the exact board revision; connector positions are not interchangeable between models.

| Board profile | SDA | SCL |
| --- | --- | --- |
| Original ESP32-S3-Touch-LCD-7 | GPIO8 | GPIO9 |
| ESP32-S3-Touch-LCD-7C-BOX | GPIO47 | GPIO48 |

The driver borrows the board's initialized 100 kHz bus, shared with touch and the IO expander. Use short wiring and avoid excessive parallel pull-ups. Place the sensor at a case opening, away from the display's own light and shadows cast by the bezel. Only one VEML7700 is supported on this bus because its address is fixed.

### Hubs, hot-plugging and panel restarts

Every sensor, cable and hub joins the same bus as the touch controller and the IO expander that controls the LCD. They also share the panel's 3.3 V rail. A fault on either can therefore affect the whole panel, not only the sensor.

**Power off the panel before connecting or disconnecting a sensor or hub.** On the 7C-BOX the 3.3 V header pin is the panel's own logic rail, with only about 21 µF of capacitance at its regulator. A module plugged in live charges its own capacitors from that rail, and the dip can reset the IO expander. The backlight enable (DISP) has a pull-down, so a reset expander turns the screen **black while the panel keeps running**. The firmware now detects this and restores the expander within about two seconds (serial log: `IO expander lost its output state ... restoring display and touch`). Hot-plugging is still not supported: a deeper dip resets the ESP32-S3 too.

On Waveshare's 7C schematic the external bus is on the board's 16-pin header **P1**: pin 4 or 6 = GND, pin 5 = **3V3**, pin 7 = SDA, pin 8 = SCL (pin 3 is 5 V; do not use it for sensors). These SDA/SCL lines are the 3.3 V side of a MOSFET level shifter (4.7 kΩ pull-ups to 3.3 V). The ESP32-S3's own GPIO47/GPIO48 run at 1.8 V behind it. Check the labels on your board.

**Gravity sensor on a STEMMA QT / Qwiic hub** (for example Adafruit 5625 with the Gravity C4001): the two systems use different pin orders and **conflicting wire colours**, so wire by signal name, never by colour:

| STEMMA QT / Qwiic (hub) | Gravity SEN0610 (sensor) |
| --- | --- |
| GND (black) | `-` (black) |
| V+ (red) | `+` (red) |
| SDA (blue) | `D/T` (green) |
| SCL (yellow) | `C/R` (blue) |

Matching blue to blue swaps SDA and SCL. Matching connectors pin-for-pin with a generic adapter can put power on a data line or short the 3.3 V rail.

The Adafruit hub's breakout pads use a **different order from its JST connectors**. The pads are labelled `VIN GND SCL SDA`, and the two rows run in opposite directions; the JST connectors are GND, V+, SDA, SCL. Wire the pads by their labels, not by position.

**Check the hub before connecting the sensor.** With only the hub attached and the panel on, the hub's green **on** LED must light and VIN-to-GND must read about **+3.3 V**. SDA and SCL to GND should also read about +3.3 V (the panel's pull-ups). If the LED stays dark or the reading is negative or 0 V, the panel-to-hub wiring is reversed or misrouted. Plugging the sensor into it reverse-powers the sensor and shorts the panel's supply. The screen goes black and USB serial drops with no log, because the panel loses power, not because firmware crashed.

- A **passive hub** such as the Adafruit 5625 simply wires its ports in parallel (it has no pull-ups). Its cables and each module's on-board pull-ups add to the bus load, so keep the total cable length short. Power the hub from the header's **3.3 V** pin: Gravity modules pull SDA/SCL up to the supply they receive, and 5 V on these lines exceeds the 3.3 V touch controller and IO expander.
- **Multiplexer hubs** (TCA9548A/PCA9548A, usually at `0x70`) are not supported. The firmware never selects a channel, so a radar behind one reports **No I2C response**.

Firmware treats a failed touch-controller read as "no touch" instead of restarting. (Previously, esp_lvgl_port aborted on any such error.) The serial log reports these failures, rate-limited, as `touch_guard: Ignored N touch read failure(s)`. Occasional messages are harmless. A continuous stream means the bus is unhealthy: touch may stop responding while the display, MQTT and web settings keep working, so you can disable the sensor under **Sensors** and power off to check the wiring.

If the panel restarts or stays dark after connecting a sensor with the power off, check the serial monitor. `Brownout detector was triggered` means the 3.3 V rail cannot carry the extra load: power the sensor from a separate 3.3 V supply with a common ground. Report any other panic backtrace along with your hub model.

## Setup and Home Assistant

1. Install this firmware, then open the panel's web settings.
2. Under **Sensors**, select **VEML7700** and a reporting interval of 1–60 seconds (default 10).
3. Configure the MQTT broker and discovery prefix as usual, then **Save & reboot**.
4. In Home Assistant, open **Settings → Devices & services → MQTT**. The panel's existing device gains an **Ambient light** measurement in `lx`, with a stable per-panel unique ID. No YAML bridge changes are needed for this sensor.

Default topics (replace `<panel-id>` with the configured ID):

- Discovery: `homeassistant/sensor/esp_home_panel_<panel-id>_illuminance/config`
- Measurement: `home/7c-panel/<panel-id>/sensor/illuminance/state`
- Sensor availability: `home/7c-panel/<panel-id>/sensor/illuminance/availability`

Discovery and availability are retained; lux measurements are not retained, so old values are not replayed after a Home Assistant restart. Both panel and sensor must be online. Readings expire after three reporting intervals plus five seconds. Discovery is republished after broker reconnect and Home Assistant birth messages. Disabling the sensor and rebooting clears its retained discovery configuration. If you change the panel ID, base topic or discovery prefix, manually remove the old discovery entry (as with other panel entities).

Absent sensors, I2C failures and saturated readings report unavailable. The driver retries on the configured interval without waiting for a conversion inside the application loop. I2C transfers have bounded 20 ms timeouts; a physically shorted shared bus can still affect touch hardware. The emulator has no sensor bus and reports unavailable.

Conversion uses gain 1/8 and 100 ms integration, with a nominal 0.5376 lx/count and high-light correction from [Vishay application note 84323, March 2025](https://www.vishay.com/docs/84323/designingveml7700.pdf). Accuracy depends on the sensor, light source and enclosure; this is not a calibrated light meter. Full-scale ADC saturation is rejected instead of reported as a valid lux value.

This feature reports ambient light; it does not automatically change screen brightness. The original LCD-7 has switch-only backlight control. Future sensor drivers can use the same polling/snapshot structure without changing touch initialization.

## Live readings in the web panel

Open the panel's web interface and select **Sensors → Live sensor readings**. The
VEML7700 lux measurement and C4001 occupied/vacant state refresh every two seconds
while this tab is visible. These readings come directly from the firmware snapshots:
no MQTT broker or Home Assistant connection is required.

The read-only `GET /api/sensors` endpoint returns JSON objects named `light`
and `presence`, with `enabled`, `available`, `age_ms`, and sensor-specific
`lux` or `occupied` fields. The endpoint uses the same administrator Basic
authentication policy as the settings page and disables response caching. Sensor
values are zeroed when unavailable; clients **must** inspect `available` before
displaying them. Stale lux samples (three configured intervals plus five seconds)
and presence samples (five seconds) are marked unavailable.

The dashboard distinguishes disabled and unavailable sensors, shows sample age,
and clears the values if the browser loses communication with the panel. This is
a near-real-time monitor based on polling, not a high-frequency streaming feed.

## Verification

`python tools/test_sensors.py` exercises the production polling code with simulated startup, missing hardware, conversion timing, dark readings, saturation, disconnection and recovery. Settings tests cover persistence and invalid input. CI builds the ESP32 firmware. Physical wiring and end-to-end discovery should also be checked on a real panel and Home Assistant installation.

## C4001 presence sensor (Gravity SEN0610)

The **Gravity C4001 24 GHz, 12-meter, I2C & UART model (SEN0610)** is supported as an optional second sensor. This firmware uses **I2C presence mode**, alongside the VEML7700 on the same bus. The UART-only 25-meter version is not supported by this driver.

### Hardware and enclosure plan

- With power off, select **I2C** on the radar and select address **0x2A** or **0x2B**. Match that address in the panel settings.
- Connect the radar's `+` to a suitable 3.3 V supply, `-` to ground, `C/R` to SCL, and `D/T` to SDA. Use the shared bus pins in the wiring table above. Check connector pin order; the Gravity PH2.0 connector is not a guarantee of compatibility with the Waveshare cable. Do not connect 5 V pull-ups to a 3.3 V bus. Confirm the supply has capacity for both sensors.
- Both sensors connect in parallel; their addresses differ. No additional UART or GPIO is required.
- Reserve a **22 × 30 mm board area**, plus connector, cable bend and mounting clearance, in the enclosure beside or above the LCD. Mount the antenna facing the room, clear of the screen's metal backing. Final mounting geometry remains pending measurements of the actual module. Test through the chosen plastic before finalizing the case; avoid metal or conductive material in front of the antenna.

### Configuration

In **Settings → Presence sensor**, select **Gravity C4001**, then save and reboot. It is disabled by default.

| Setting | Default | Allowed values |
| --- | --- | --- |
| I2C address | 0x2A | 0x2A, 0x2B |
| Maximum detection range | 6 m | 3–8 m, whole meters |
| Trigger sensitivity | 5 | 0–9 |
| Hold sensitivity | 5 | 0–9 |
| Vacancy delay | 30 seconds | 2–120 seconds |

The firmware sets the minimum range to 0.3 m and the trigger range equal to the maximum. These are configuration bounds, not a guarantee of detection at every distance. The manufacturer specifies presence detection up to 8 m; the advertised 12 m is the motion/ranging limit. Adjust sensitivity and range in the installed room, checking fans, curtains, pets, and activity beyond the intended area.

The radar implements the vacancy delay: occupancy remains asserted after detection until its configured timeout expires. Firmware polls approximately every 0.5 seconds and publishes every second, independently of the lux interval. Mode changes and save/start/stop delays are scheduled across application-loop iterations. I2C transfers have bounded timeouts. Configuration is read back before publishing valid readings and saved to the radar only when its current parameters differ.

### Home Assistant

An **Occupancy** MQTT binary sensor appears under the existing panel device. Its device class is `occupancy`; raw state messages are `ON` and `OFF`.

- Discovery: `<discovery-prefix>/binary_sensor/esp_home_panel_<panel-id>_occupancy/config`
- State: `<base-topic>/<panel-id>/sensor/occupancy/state`
- Sensor availability: `<base-topic>/<panel-id>/sensor/occupancy/availability`

Discovery and sensor availability are retained. Occupancy state is not retained and expires after five seconds without updates. Both panel and radar must be available. Startup, configuration, I2C errors and stale samples report **unavailable**, never a synthetic vacant-room reading. The driver retries after five seconds and reuses the device handle. Discovery is republished on MQTT reconnect and Home Assistant birth. Disabling the sensor and rebooting removes its discovery entry. Changing MQTT identifiers requires cleaning up the old discovery entry as described above.

No Home Assistant YAML bridge change is required. This addition does not change screen behavior automatically, and does not expose distance or speed from the mutually exclusive ranging mode.

### Troubleshooting detection in the web panel

If **Live sensor readings** reports the SEN0610 as unavailable, open
**Sensors** and read the diagnostic text under **Room occupancy**. The
diagnostic is also present in the read-only `GET /api/sensors` response
under `presence.diagnostic`, with `detected`, `other_address`,
`address`, and optionally `raw_status`. The serial console logs each change
once, for example `W (12345) c4001: Radar 0x2A: No I2C response: check wiring and I2C mode switch`.
While the radar is unavailable, MQTT publishes `offline` on the occupancy
availability topic; that message alone does not say why.

- **No I2C response**: verify the SEN0610's mode switch is on I2C,
  the +/− power and ground, the C/R-to-SCL and D/T-to-SDA wiring, and
  that the cable pin ordering matches on both ends. Powered LEDs do not
  prove the data lines are communicating. The ESP32 bus is 3.3 V only.
- **Radar responds at the other I2C address**: change the configured
  address between 0x2A and 0x2B, save, and reboot.
- **Radar is responding but not initialized**: wait briefly for sensor
  startup, then power-cycle the panel and radar if persistent.
- **Configuration readback differs / cannot start**: the radar is detected
  but cannot complete the presence-mode setup; check the selected I2C
  mode, cable reliability, and available power. Note the raw status value.
- **Radar connected**: the firmware has read a valid occupancy status.
  Any `available=false` after that may indicate stale readings or
  a new communications failure.

The firmware retries after failures. It probes both *supported radar
addresses* only while initially opening the device; it does not scan arbitrary
bus addresses or interfere with unrelated devices. Existing sensors are
not toggled into a new mode for diagnostic purposes.

### Protocol and verification

The driver follows DFRobot's [official C4001 register definitions and I2C implementation](https://github.com/DFRobot/DFRobot_C4001/tree/master/src). Hardware specifications and mounting guidance: [SEN0610 documentation](https://wiki.dfrobot.com/sen0610/).

Run `python tools/test_c4001.py`, `python tools/test_presence_mqtt.py`, and `python tools/test_color_settings.py`. They cover register encoding, command delays, mode changes, configuration readback, disconnection/recovery, both addresses, saved settings, MQTT discovery and availability. CI also builds the complete firmware and renders the display themes. Actual detection quality, power budget and end-to-end Home Assistant operation still require the physical panel and sensor.
