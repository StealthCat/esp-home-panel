# Home Assistant MQTT bridge

The ESP Home Panel uses Mosquitto as an MQTT transport, while Home Assistant
performs the actual actions against existing Home Assistant entities.

Mosquitto does not expose Home Assistant's entity registry to MQTT clients and
does not interpret an entity ID such as `light.office` as a command by itself.
The panel therefore publishes a JSON command containing the configured
`entity_id`, and the included Home Assistant automation turns that MQTT message
into a Home Assistant action.

## Multiple panels

Multiple panels are supported by design.

All panels can use the same:

- Mosquitto broker
- MQTT credentials
- MQTT base topic prefix
- Home Assistant bridge package

Each physical panel has its own **Panel ID**. On a fresh device the firmware
creates a stable default from the final three bytes of its Wi-Fi MAC address,
for example:

```text
panel-a1b2c3
panel-d4e5f6
```

You can rename these in the panel's web configuration to readable IDs such as:

```text
kitchen-panel
office-panel
bedroom-panel
```

Panel IDs must be unique under a given MQTT base topic. Allowed characters are
lowercase letters, digits, `-`, and `_`.

With the default MQTT base topic `home/7c-panel`, two panels use separate topic
trees:

```text
home/7c-panel/kitchen-panel/...
home/7c-panel/office-panel/...
```

For example:

```text
home/7c-panel/kitchen-panel/cmd/tapo
home/7c-panel/kitchen-panel/state/tapo

home/7c-panel/office-panel/cmd/tapo
home/7c-panel/office-panel/state/tapo
```

Commands, state, availability, and state-refresh traffic are therefore isolated
per panel. Two panels may control different Home Assistant entities, or they may
both intentionally target the same entity.

ESP-MQTT also generates a unique default MQTT client ID from each ESP32's MAC
address, so panels do not disconnect one another at the broker simply because
they use the same firmware image.

## 1. Prerequisites

- Home Assistant MQTT integration configured and connected to the same
  Mosquitto broker as the panels.
- An MQTT login for the panels if your broker requires authentication.
- A common MQTT base topic. The firmware default is `home/7c-panel`.

## 2. Install the bridge

The file `esp-home-panel-mqtt-bridge.yaml` is written as a Home Assistant
package.

Only **one copy** of the package is needed for all panels sharing a base topic.

If packages are already enabled, copy it to a package directory such as:

```text
/config/packages/esp_home_panel.yaml
```

A common package configuration is:

```yaml
homeassistant:
  packages: !include_dir_named packages
```

If you do not use packages, merge the `script:` and `automation:` sections
from the bridge file into the corresponding Home Assistant configuration.

The included bridge listens to:

```text
home/7c-panel/+/cmd/+
home/7c-panel/+/request_state
```

The first `+` is the panel ID.

If your MQTT base topic is not `home/7c-panel`, replace only that prefix in
the two bridge trigger topics. For example, for `house/panels`:

```text
house/panels/+/cmd/+
house/panels/+/request_state
```

The bridge derives each individual panel's response topic from the incoming
topic, so no per-panel Home Assistant configuration is required.

Restart Home Assistant or reload the affected YAML configuration after
installation.

## 3. Configure each panel

Open each panel's web configuration page.

Under **MQTT**:

1. Use the same broker and base topic on all panels.
2. Give every panel a unique Panel ID.
3. Verify the displayed effective topic root.

Under **Home Assistant targets**, choose the entities for that particular panel.

| Panel control | Home Assistant target |
| --- | --- |
| Tapo LED strips | `light.*` |
| Overhead light | `light.*` |
| Ceiling fan | `fan.*` or `switch.*` |
| Echo | `media_player.*` |
| Echo announcement | optional `script.*` |

Examples for an office panel:

```text
light.office_led_strips
light.office_overhead
fan.office_ceiling_fan
media_player.office_echo
script.office_echo_announce
```

A kitchen panel can use an entirely different set of entity IDs while sharing
the same broker, credentials, base topic, and Home Assistant bridge.

The announcement button invokes the configured script. This is intentionally a
script target because announcement/TTS support differs between media-player
integrations. Put whatever TTS or Alexa-specific action you need in that Home
Assistant script.

## MQTT protocol

Commands retain the existing panel JSON fields and add the Home Assistant target.

Example from `office-panel`:

```text
Topic: home/7c-panel/office-panel/cmd/tapo
Payload: {"brightness":60,"entity_id":"light.office_led_strips"}
```

The bridge calls the appropriate Home Assistant action and publishes normalized
state back to that same panel's namespace:

```text
home/7c-panel/office-panel/state/tapo
home/7c-panel/office-panel/state/overhead
home/7c-panel/office-panel/state/fan
home/7c-panel/office-panel/state/echo
```

State messages are retained. Every panel independently publishes a state request
every 10 seconds containing its configured entity IDs, so controls stay
synchronized when an entity is changed from a Home Assistant dashboard,
automation, voice assistant, or another device.

Example:

```json
{
  "request": 1,
  "panel_id": "office-panel",
  "tapo": "light.office_led_strips",
  "overhead": "light.office_overhead",
  "fan": "fan.office_ceiling_fan",
  "echo": "media_player.office_echo"
}
```

The Home Assistant automation and state-publish script both run in parallel
mode, allowing simultaneous commands and refreshes from multiple panels.

## Why the web page uses entity-ID fields instead of dropdowns

An MQTT client can subscribe and publish through Mosquitto, but the broker does
not provide Home Assistant's entity registry. Enumerating Home Assistant
entities would require a separate authenticated Home Assistant REST/WebSocket
connection.

Keeping entity selection as entity-ID fields means each panel only needs its
MQTT credentials and never needs a Home Assistant long-lived access token.
