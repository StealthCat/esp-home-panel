#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_WIFI_SSID_MAX       32
#define PANEL_WIFI_PASSWORD_MAX   64
#define PANEL_MQTT_URI_MAX        191
#define PANEL_MQTT_USERNAME_MAX   63
#define PANEL_MQTT_PASSWORD_MAX   127
#define PANEL_MQTT_BASE_TOPIC_MAX 127
#define PANEL_HA_ENTITY_ID_MAX    127
#define PANEL_TIMEZONE_MAX        95

typedef struct {
    char wifi_ssid[PANEL_WIFI_SSID_MAX + 1];
    char wifi_password[PANEL_WIFI_PASSWORD_MAX + 1];

    char mqtt_uri[PANEL_MQTT_URI_MAX + 1];
    char mqtt_username[PANEL_MQTT_USERNAME_MAX + 1];
    char mqtt_password[PANEL_MQTT_PASSWORD_MAX + 1];
    char mqtt_base_topic[PANEL_MQTT_BASE_TOPIC_MAX + 1];

    /*
     * Existing Home Assistant entities controlled through the MQTT bridge.
     * These are entity IDs such as light.kitchen or media_player.office_echo.
     * Empty strings leave that control unmapped.
     */
    char ha_tapo_entity[PANEL_HA_ENTITY_ID_MAX + 1];
    char ha_overhead_entity[PANEL_HA_ENTITY_ID_MAX + 1];
    char ha_fan_entity[PANEL_HA_ENTITY_ID_MAX + 1];
    char ha_echo_entity[PANEL_HA_ENTITY_ID_MAX + 1];
    char ha_echo_announce_script[PANEL_HA_ENTITY_ID_MAX + 1];

    char timezone[PANEL_TIMEZONE_MAX + 1];

    uint8_t theme;
    uint8_t backlight_percent;
} panel_settings_t;

esp_err_t settings_init(void);
const panel_settings_t *settings_get(void);
esp_err_t settings_save(const panel_settings_t *settings);
esp_err_t settings_reset(void);

#ifdef __cplusplus
}
#endif
