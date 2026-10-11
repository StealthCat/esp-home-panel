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

    uint8_t ambient_light_enabled;
    uint8_t sensor_interval_seconds; /* 1-60 seconds */
    uint8_t presence_enabled;
    uint8_t presence_address;
    uint8_t presence_max_m;
    uint8_t presence_trigger_sensitivity;
    uint8_t presence_keep_sensitivity;
    uint8_t presence_hold_seconds;
    uint8_t sleep_enabled;
    uint8_t sleep_delay_seconds;
    uint8_t touch_wake_minutes;
    uint8_t auto_brightness;
    uint8_t brightness_min;
    uint8_t brightness_max;
    uint8_t weather_enabled;
    uint8_t weather_provider; /* 0 Weather Underground, 1 Ambient Weather */
    uint8_t weather_forecast; /* 0 Open-Meteo, 1 Weather Underground, 2 disabled */
    uint8_t weather_units; /* 0 imperial, 1 metric */
    uint8_t weather_poll_minutes;
    char weather_station[128];
    char weather_api_key[129];
    char weather_app_key[129];
    char weather_forecast_key[129];
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


