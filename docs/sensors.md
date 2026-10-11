# Sensors

The initial sensor implementation supports one **VEML7700 ambient light breakout** at fixed I2C address `0x10`. It is disabled by default. Other sensor models require their own drivers; enabling this setting does not add a sensor inside the panel.

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
