# Web configuration

Open the panel's network address in a browser. Settings are grouped into five tabs:

- **Network:** Wi-Fi credentials, hostname, and current network names.
- **Display:** title, theme, backlight, timezone, and light-strip color presets.
- **Home Assistant:** MQTT connection, discovery, and entity mappings.
- **Sensors:** VEML7700 ambient light and Gravity C4001 presence settings.
- **System:** administrator password, firmware updater, and configuration reset.

Edits remain in the form when switching tabs. **Save & restart** saves all tabs together and restarts the panel. The save bar indicates unsaved changes; **Discard changes** restores the values loaded when the page opened. Leaving the page with unsaved edits triggers the browser's warning. Password fields remain blank on load; leaving them blank keeps the existing passwords.

If a field fails browser validation, its tab opens and the field receives focus. A server error appears above the tabs while preserving the entered values. After a successful save, reconnect once the panel restarts. A Wi-Fi or hostname change may require connecting to a different address.

The tabs support arrow keys, Home, and End. On phones, all tabs remain visible in two rows. Styling and scripts are served by the panel with no external fonts or internet dependencies. Without JavaScript, settings remain available as one complete form.

Firmware uploads use the dedicated updater linked from **System**. Configuration reset has a separate confirmation and restores firmware defaults, including administrator access and network settings.

## Development checks

`python tools/render_web_config.py` prints the production handler's HTML using a host fixture with fake network/settings data. `node tools/test_settings_ui.js` uses Playwright to exercise that HTML at desktop and mobile sizes; CI runs these browser checks and retains screenshots as the `settings-ui-previews` artifact. The fixture never contacts a physical panel.
