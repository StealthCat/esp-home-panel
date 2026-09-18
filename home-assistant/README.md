# Home Assistant MQTT bridge

The ESP-IDF firmware deliberately uses MQTT instead of embedding Home Assistant
credentials in the display. Home Assistant remains responsible for talking to
the Tapo strip, dimmer, fan switch, and Echo integration.

1. Make sure Home Assistant and the 7C BOX can reach the same MQTT broker.
2. Edit the four entity IDs in `esp-home-panel-package.yaml` if needed.
3. Install the file as a Home Assistant package, or copy its automations into
   `automations.yaml`.
4. Configure the firmware's MQTT URI, username/password, and base topic with:

   ```bash
   cd idf
   idf.py menuconfig
   ```

The default base topic is `home/7c-panel`.

The **Announce** button emits the Home Assistant event
`esp_home_panel_announce`. This is intentional because Alexa announcement/TTS
services differ by integration. Add one small HA automation that listens for
that event and calls your preferred Alexa notification service.
