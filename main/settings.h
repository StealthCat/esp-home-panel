#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_WIFI_SSID_MAX       32
#define PANEL_HOSTNAME_MAX        32
#define PANEL_ADMIN_PASSWORD_MAX  64
#define PANEL_WIFI_PASSWORD_MAX   64
#define PANEL_MQTT_URI_MAX        191
#define PANEL_MQTT_USERNAME_MAX   63
#define PANEL_MQTT_PASSWORD_MAX   127
#define PANEL_MQTT_BASE_TOPIC_MAX       127
#define PANEL_MQTT_DISCOVERY_PREFIX_MAX 63
#define PANEL_ID_MAX                    31
#define PANEL_DISPLAY_TITLE_MAX          31
#define PANEL_HA_ENTITY_ID_MAX    127
#define PANEL_TIMEZONE_MAX        95
#define PANEL_COLOR_PRESET_COUNT   6

typedef struct {
    char hostname[PANEL_HOSTNAME_MAX + 1];
    /* Empty on first install; OTA requires an administrator password. */
    char admin_password[PANEL_ADMIN_PASSWORD_MAX + 1];
    char wifi_ssid[PANEL_WIFI_SSID_MAX + 1];
    char wifi_password[PANEL_WIFI_PASSWORD_MAX + 1];

    char mqtt_uri[PANEL_MQTT_URI_MAX + 1];
    char mqtt_username[PANEL_MQTT_USERNAME_MAX + 1];
    char mqtt_password[PANEL_MQTT_PASSWORD_MAX + 1];
    char mqtt_base_topic[PANEL_MQTT_BASE_TOPIC_MAX + 1];
    char mqtt_discovery_prefix[PANEL_MQTT_DISCOVERY_PREFIX_MAX + 1];

    /*
     * Unique MQTT namespace segment for this physical panel. Defaults to a
     * stable value derived from the Wi-Fi STA MAC address.
     */
    char panel_id[PANEL_ID_MAX + 1];

    /* User-configurable title shown in the display header. */
    char display_title[PANEL_DISPLAY_TITLE_MAX + 1];

    /*
     * Existing Home Assistant entities controlled through the MQTT bridge.
     * These are entity IDs such as light.kitchen or media_player.office_echo.
     * Empty strings leave that control unmapped.
     */
    char ha_tapo_entity[PANEL_HA_ENTITY_ID_MAX + 1];
    char ha_overhead_entity[PANEL_HA_ENTITY_ID_MAX + 1];
    char ha_fan_entity[PANEL_HA_ENTITY_ID_MAX + 1];
    char ha_echo_entity[PANEL_HA_ENTITY_ID_MAX + 1];

    char timezone[PANEL_TIMEZONE_MAX + 1];

    /* RGB values for the six Tapo circles, ordered left to right. */
    uint32_t color_presets[PANEL_COLOR_PRESET_COUNT];

    uint8_t theme;
    uint8_t backlight_percent;
} panel_settings_t;

esp_err_t settings_init(void);
bool settings_valid_hostname(const char *hostname);
const panel_settings_t *settings_get(void);
esp_err_t settings_save(const panel_settings_t *settings);
esp_err_t settings_reset(void);

#ifdef __cplusplus
}
#endif


