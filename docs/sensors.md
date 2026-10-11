# Sensors

The firmware supports one **VEML7700 ambient light breakout** at fixed I2C address `0x10` and one optional **Gravity C4001 SEN0610 presence sensor** (described below). Both are disabled by default. Other sensor models require their own drivers; enabling these settings does not add sensors inside the panel.

## Wiring

Power off before connecting a 3.3 V compatible breakout. Connect ground, 3.3 V, SDA and SCL to the panel's existing I2C bus. Do not connect 5 V pull-ups to ESP32 GPIOs. Verify connector labels against the exact board revision; connector positions are not interchangeable between models.

| Board profile | SDA | SCL |
| --- | --- | --- |
| Original ESP32-S3-Touch-LCD-7 | GPIO8 | GPIO9 |
| ESP32-S3-Touch-LCD-7C-BOX | GPIO47 | GPIO48 |

The driver borrows the board's initialized 100 kHz bus, shared with touch and the IO expander. Use short wiring and avoid excessive parallel pull-ups. Place the sensor at a case opening, away from the display's own light and shadows cast by the bezel. Only one VEML7700 is supported on this bus because its address is fixed.

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

### Protocol and verification

The driver follows DFRobot's [official C4001 register definitions and I2C implementation](https://github.com/DFRobot/DFRobot_C4001/tree/master/src). Hardware specifications and mounting guidance: [SEN0610 documentation](https://wiki.dfrobot.com/sen0610/).

Run `python tools/test_c4001.py`, `python tools/test_presence_mqtt.py`, and `python tools/test_color_settings.py`. They cover register encoding, command delays, mode changes, configuration readback, disconnection/recovery, both addresses, saved settings, MQTT discovery and availability. CI also builds the complete firmware and renders the display themes. Actual detection quality, power budget and end-to-end Home Assistant operation still require the physical panel and sensor.
