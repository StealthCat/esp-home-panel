# Home Assistant MQTT bridge

The ESP Home Panel uses Mosquitto as an MQTT transport, while Home Assistant
performs the actual actions against existing Home Assistant entities.

Mosquitto does not expose Home Assistant's entity registry to MQTT clients and
does not interpret an entity ID such as `light.office` as a command by itself.
The panel therefore publishes a JSON command containing the configured
`entity_id`, and the included Home Assistant automation turns that MQTT message
into a Home Assistant action.

## 1. Prerequisites

- Home Assistant MQTT integration configured and connected to the same
  Mosquitto broker as the panel.
- A dedicated MQTT login for the panel if your broker requires authentication.
- The panel's MQTT base topic. The firmware default is `home/7c-panel`.

## 2. Install the bridge

The file `esp-home-panel-mqtt-bridge.yaml` is written as a Home Assistant
package.

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

If the panel MQTT base topic is not `home/7c-panel`, change the two MQTT
trigger topics in the bridge:

```text
<base topic>/cmd/+
<base topic>/request_state
```

The bridge derives all response topics from the incoming topic, so those are the
only topic strings that need changing.

Restart Home Assistant or reload the affected YAML configuration after
installation.

## 3. Configure the panel

Open the panel's web configuration page and fill in **Home Assistant targets**.

| Panel control | Home Assistant target |
| --- | --- |
| Tapo LED strips | `light.*` |
| Overhead light | `light.*` |
| Ceiling fan | `fan.*` or `switch.*` |
| Echo | `media_player.*` |
| Echo announcement | optional `script.*` |

Examples:

```text
light.office_led_strips
light.office_overhead
fan.office_ceiling_fan
media_player.office_echo
script.office_echo_announce
```

The announcement button invokes the configured script. This is intentionally a
script target because announcement/TTS support differs between media-player
integrations. Put whatever TTS or Alexa-specific action you need in that Home
Assistant script.

## MQTT protocol

Commands retain the existing panel JSON fields and add the Home Assistant target.

Example:

```text
Topic: home/7c-panel/cmd/tapo
Payload: {"brightness":60,"entity_id":"light.office_led_strips"}
```

The bridge calls the appropriate Home Assistant action and publishes normalized
state to:

```text
home/7c-panel/state/tapo
home/7c-panel/state/overhead
home/7c-panel/state/fan
home/7c-panel/state/echo
```

State messages are retained. The panel also publishes a state request every
10 seconds containing its configured entity IDs, so controls stay synchronized
when an entity is changed from a Home Assistant dashboard, automation, voice
assistant, or another device.

Example request:

```json
{
  "request": 1,
  "tapo": "light.office_led_strips",
  "overhead": "light.office_overhead",
  "fan": "fan.office_ceiling_fan",
  "echo": "media_player.office_echo"
}
```

## Why the web page uses entity-ID fields instead of dropdowns

An MQTT client can subscribe and publish through Mosquitto, but the broker does
not provide Home Assistant's entity registry. Enumerating Home Assistant
entities would require a separate authenticated Home Assistant REST/WebSocket
connection.

Keeping entity selection as entity-ID fields means the panel only needs its MQTT
credentials and never needs a Home Assistant long-lived access token.
