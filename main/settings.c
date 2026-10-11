#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "settings";
static const char *NAMESPACE = "panelcfg";

static panel_settings_t *s_settings;
static bool s_initialized;

static esp_err_t ensure_storage(void)
{
    if (s_settings) {
        return ESP_OK;
    }

    s_settings = heap_caps_calloc(
        1, sizeof(*s_settings), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_settings) {
        s_settings = calloc(1, sizeof(*s_settings));
    }

    return s_settings ? ESP_OK : ESP_ERR_NO_MEM;
}

static uint8_t default_theme(void)
{
#if CONFIG_PANEL_THEME_SOCCER
    return 1;
#elif CONFIG_PANEL_THEME_KITTENS
    return 2;
#elif CONFIG_PANEL_THEME_MAGIC
    return 3;
#else
    return 0;
#endif
}

static void default_panel_id(char *dst, size_t dst_size)
{
    uint8_t mac[6] = {0};
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err == ESP_OK) {
        snprintf(dst, dst_size, "panel-%02x%02x%02x",
                 mac[3], mac[4], mac[5]);
        return;
    }

    strlcpy(dst, "panel-default", dst_size);
    ESP_LOGW(TAG, "Unable to derive panel ID from MAC: %s", esp_err_to_name(err));
}

static void load_defaults(panel_settings_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    default_panel_id(cfg->hostname, sizeof(cfg->hostname));

    strlcpy(cfg->wifi_ssid, CONFIG_PANEL_WIFI_SSID, sizeof(cfg->wifi_ssid));
    strlcpy(cfg->wifi_password, CONFIG_PANEL_WIFI_PASSWORD, sizeof(cfg->wifi_password));

    strlcpy(cfg->mqtt_uri, CONFIG_PANEL_MQTT_URI, sizeof(cfg->mqtt_uri));
    strlcpy(cfg->mqtt_username, CONFIG_PANEL_MQTT_USERNAME, sizeof(cfg->mqtt_username));
    strlcpy(cfg->mqtt_password, CONFIG_PANEL_MQTT_PASSWORD, sizeof(cfg->mqtt_password));
    strlcpy(cfg->mqtt_base_topic, CONFIG_PANEL_MQTT_BASE_TOPIC, sizeof(cfg->mqtt_base_topic));
    strlcpy(cfg->mqtt_discovery_prefix, CONFIG_PANEL_MQTT_DISCOVERY_PREFIX,
            sizeof(cfg->mqtt_discovery_prefix));
    default_panel_id(cfg->panel_id, sizeof(cfg->panel_id));
    strlcpy(cfg->display_title, CONFIG_PANEL_DISPLAY_TITLE, sizeof(cfg->display_title));

    strlcpy(cfg->timezone, CONFIG_PANEL_TIMEZONE, sizeof(cfg->timezone));

    const uint32_t colors[PANEL_COLOR_PRESET_COUNT] = {
        0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x0066FF, 0xA020F0
    };
    memcpy(cfg->color_presets, colors, sizeof(colors));

    cfg->sensor_interval_seconds = 10;
    cfg->presence_enabled = 0;
    cfg->presence_address = 42;
    cfg->presence_max_m = 6;
    cfg->presence_trigger_sensitivity = 5;
    cfg->presence_keep_sensitivity = 5;
    cfg->presence_hold_seconds = 30;

    cfg->sleep_enabled = 0;
    cfg->sleep_delay_seconds = 60;
    cfg->touch_wake_minutes = 2;
    cfg->auto_brightness = 0;
    cfg->brightness_min = 10;
    cfg->brightness_max = 100;
    cfg->weather_enabled = 0;
    cfg->weather_poll_minutes = 1;
    cfg->theme = default_theme();
    cfg->backlight_percent = CONFIG_PANEL_BACKLIGHT;
}

static void load_string(nvs_handle_t nvs, const char *key, char *dst, size_t dst_size)
{
    size_t required = dst_size;
    esp_err_t err = nvs_get_str(nvs, key, dst, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to load %s from NVS: %s", key, esp_err_to_name(err));
    }
}

esp_err_t settings_init(void)
{
    esp_err_t err = ensure_storage();
    if (err != ESP_OK) {
        return err;
    }

    load_defaults(s_settings);

    nvs_handle_t nvs;
    err = nvs_open(NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_initialized = true;
        ESP_LOGI(TAG, "No saved web configuration; using build defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    load_string(nvs, "wifi_ssid", s_settings->wifi_ssid, sizeof(s_settings->wifi_ssid));
    load_string(nvs, "hostname", s_settings->hostname, sizeof(s_settings->hostname));
    load_string(nvs, "admin_pass", s_settings->admin_password, sizeof(s_settings->admin_password));
    if (!settings_valid_hostname(s_settings->hostname)) {
        default_panel_id(s_settings->hostname, sizeof(s_settings->hostname));
    }
    load_string(nvs, "wifi_pass", s_settings->wifi_password, sizeof(s_settings->wifi_password));
    load_string(nvs, "mqtt_uri", s_settings->mqtt_uri, sizeof(s_settings->mqtt_uri));
    load_string(nvs, "mqtt_user", s_settings->mqtt_username, sizeof(s_settings->mqtt_username));
    load_string(nvs, "mqtt_pass", s_settings->mqtt_password, sizeof(s_settings->mqtt_password));
    load_string(nvs, "mqtt_topic", s_settings->mqtt_base_topic, sizeof(s_settings->mqtt_base_topic));
    load_string(nvs, "mqtt_disc", s_settings->mqtt_discovery_prefix, sizeof(s_settings->mqtt_discovery_prefix));
    load_string(nvs, "panel_id", s_settings->panel_id, sizeof(s_settings->panel_id));
    load_string(nvs, "display_title", s_settings->display_title, sizeof(s_settings->display_title));
    load_string(nvs, "ha_tapo", s_settings->ha_tapo_entity, sizeof(s_settings->ha_tapo_entity));
    load_string(nvs, "ha_over", s_settings->ha_overhead_entity, sizeof(s_settings->ha_overhead_entity));
    load_string(nvs, "ha_fan", s_settings->ha_fan_entity, sizeof(s_settings->ha_fan_entity));
    load_string(nvs, "ha_echo", s_settings->ha_echo_entity, sizeof(s_settings->ha_echo_entity));
    load_string(nvs, "timezone", s_settings->timezone, sizeof(s_settings->timezone));

    for (int i = 0; i < PANEL_COLOR_PRESET_COUNT; ++i) {
        char key[16];
        uint32_t rgb;
        snprintf(key, sizeof(key), "color_%d", i);
        if (nvs_get_u32(nvs, key, &rgb) == ESP_OK && rgb <= 0xFFFFFF) {
            s_settings->color_presets[i] = rgb;
        }
    }

    load_string(nvs, "wx_station", s_settings->weather_station, sizeof(s_settings->weather_station));
    load_string(nvs, "wx_api", s_settings->weather_api_key, sizeof(s_settings->weather_api_key));
    load_string(nvs, "wx_app", s_settings->weather_app_key, sizeof(s_settings->weather_app_key));
    load_string(nvs, "wx_fc_key", s_settings->weather_forecast_key, sizeof(s_settings->weather_forecast_key));
    uint8_t value = 0;
    if (nvs_get_u8(nvs, "sleep_on", &value) == ESP_OK && value <= 1) s_settings->sleep_enabled = value;
    if (nvs_get_u8(nvs, "sleep_delay", &value) == ESP_OK && value >= 5 && value <= 240) s_settings->sleep_delay_seconds = value;
    if (nvs_get_u8(nvs, "wake_minutes", &value) == ESP_OK && value >= 1 && value <= 30) s_settings->touch_wake_minutes = value;
    if (nvs_get_u8(nvs, "auto_bright", &value) == ESP_OK && value <= 1) s_settings->auto_brightness = value;
    if (nvs_get_u8(nvs, "bright_min", &value) == ESP_OK && value >= 5 && value <= 100) s_settings->brightness_min = value;
    if (nvs_get_u8(nvs, "bright_max", &value) == ESP_OK && value >= 5 && value <= 100) s_settings->brightness_max = value;
    if (nvs_get_u8(nvs, "weather_on", &value) == ESP_OK && value <= 1) s_settings->weather_enabled = value;
    if (s_settings->brightness_min > s_settings->brightness_max) s_settings->brightness_min = s_settings->brightness_max;

    if (nvs_get_u8(nvs, "prs_enabled", &value) == ESP_OK && value <= 1)
        s_settings->presence_enabled = value;
    if (nvs_get_u8(nvs, "prs_address", &value) == ESP_OK && value >= 42 && value <= 43)
        s_settings->presence_address = value;
    if (nvs_get_u8(nvs, "prs_max_m", &value) == ESP_OK && value >= 3 && value <= 8)
        s_settings->presence_max_m = value;
    if (nvs_get_u8(nvs, "prs_trig_sens", &value) == ESP_OK && value <= 9)
        s_settings->presence_trigger_sensitivity = value;
    if (nvs_get_u8(nvs, "prs_keep_sens", &value) == ESP_OK && value <= 9)
        s_settings->presence_keep_sensitivity = value;
    if (nvs_get_u8(nvs, "prs_hold_s", &value) == ESP_OK && value >= 2 && value <= 120)
        s_settings->presence_hold_seconds = value;

    if (nvs_get_u8(nvs, "als_enabled", &value) == ESP_OK && value <= 1)
        s_settings->ambient_light_enabled = value;
    if (nvs_get_u8(nvs, "sensor_interval", &value) == ESP_OK && value >= 1 && value <= 60)
        s_settings->sensor_interval_seconds = value;
    if (nvs_get_u8(nvs, "theme", &value) == ESP_OK && value <= 3) {
        s_settings->theme = value;
    }
    if (nvs_get_u8(nvs, "backlight", &value) == ESP_OK && value <= 100) {
        s_settings->backlight_percent = value;
    }

    if (nvs_get_u8(nvs, "wx_provider", &value) == ESP_OK && value <= 1) s_settings->weather_provider = value;
    if (nvs_get_u8(nvs, "wx_forecast", &value) == ESP_OK && value <= 2) s_settings->weather_forecast = value;
    if (nvs_get_u8(nvs, "wx_units", &value) == ESP_OK && value <= 1) s_settings->weather_units = value;
    if (nvs_get_u8(nvs, "wx_poll", &value) == ESP_OK && value <= 30 && value >= 1) s_settings->weather_poll_minutes = value;
    nvs_close(nvs);
    s_initialized = true;
    ESP_LOGI(TAG, "Loaded saved panel configuration");
    return ESP_OK;
}

const panel_settings_t *settings_get(void)
{
    if (!s_settings && ensure_storage() != ESP_OK) {
        return NULL;
    }

    if (!s_initialized) {
        load_defaults(s_settings);
        s_initialized = true;
    }

    return s_settings;
}

bool settings_valid_hostname(const char *hostname)
{
    if (!hostname) return false;
    size_t len = 0;
    while (len <= PANEL_HOSTNAME_MAX && hostname[len]) ++len;
    if (!len || len > PANEL_HOSTNAME_MAX || hostname[0] == '-' || hostname[len - 1] == '-') {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        char c = hostname[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

esp_err_t settings_save(const panel_settings_t *settings)
{
    if (!settings || !settings_valid_hostname(settings->hostname)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_storage();
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < PANEL_COLOR_PRESET_COUNT; ++i) {
        if (settings->color_presets[i] > 0xFFFFFF) return ESP_ERR_INVALID_ARG;
    }

    if (settings->ambient_light_enabled > 1 || settings->sensor_interval_seconds < 1 ||
        settings->sensor_interval_seconds > 60) return ESP_ERR_INVALID_ARG;

    if (settings->presence_enabled > 1) return ESP_ERR_INVALID_ARG;
    if (settings->presence_address < 42 || settings->presence_address > 43) return ESP_ERR_INVALID_ARG;
    if (settings->presence_max_m < 3 || settings->presence_max_m > 8) return ESP_ERR_INVALID_ARG;
    if (settings->presence_trigger_sensitivity > 9) return ESP_ERR_INVALID_ARG;
    if (settings->presence_keep_sensitivity > 9) return ESP_ERR_INVALID_ARG;
    if (settings->presence_hold_seconds < 2 || settings->presence_hold_seconds > 120) return ESP_ERR_INVALID_ARG;

    if (settings->sleep_enabled > 1) return ESP_ERR_INVALID_ARG;
    if (settings->sleep_delay_seconds < 5 || settings->sleep_delay_seconds > 240) return ESP_ERR_INVALID_ARG;
    if (settings->touch_wake_minutes < 1 || settings->touch_wake_minutes > 30) return ESP_ERR_INVALID_ARG;
    if (settings->auto_brightness > 1) return ESP_ERR_INVALID_ARG;
    if (settings->brightness_min < 5 || settings->brightness_min > 100) return ESP_ERR_INVALID_ARG;
    if (settings->brightness_max < 5 || settings->brightness_max > 100) return ESP_ERR_INVALID_ARG;
    if (settings->weather_enabled > 1) return ESP_ERR_INVALID_ARG;
    if (settings->brightness_min > settings->brightness_max) return ESP_ERR_INVALID_ARG;
    if (settings->weather_provider > 1) return ESP_ERR_INVALID_ARG;
    if (settings->weather_forecast > 2) return ESP_ERR_INVALID_ARG;
    if (settings->weather_units > 1) return ESP_ERR_INVALID_ARG;
    if (settings->weather_poll_minutes > 30 || settings->weather_poll_minutes < 1) return ESP_ERR_INVALID_ARG;
    panel_settings_t clean = *settings;
    clean.weather_station[127] = 0;
    clean.weather_api_key[128] = clean.weather_app_key[128] = clean.weather_forecast_key[128] = 0;
    clean.wifi_ssid[PANEL_WIFI_SSID_MAX] = '\0';
    clean.admin_password[PANEL_ADMIN_PASSWORD_MAX] = '\0';
    clean.wifi_password[PANEL_WIFI_PASSWORD_MAX] = '\0';
    clean.mqtt_uri[PANEL_MQTT_URI_MAX] = '\0';
    clean.mqtt_username[PANEL_MQTT_USERNAME_MAX] = '\0';
    clean.mqtt_password[PANEL_MQTT_PASSWORD_MAX] = '\0';
    clean.mqtt_base_topic[PANEL_MQTT_BASE_TOPIC_MAX] = '\0';
    clean.mqtt_discovery_prefix[PANEL_MQTT_DISCOVERY_PREFIX_MAX] = '\0';
    clean.panel_id[PANEL_ID_MAX] = '\0';
    clean.display_title[PANEL_DISPLAY_TITLE_MAX] = '\0';
    clean.ha_tapo_entity[PANEL_HA_ENTITY_ID_MAX] = '\0';
    clean.ha_overhead_entity[PANEL_HA_ENTITY_ID_MAX] = '\0';
    clean.ha_fan_entity[PANEL_HA_ENTITY_ID_MAX] = '\0';
    clean.ha_echo_entity[PANEL_HA_ENTITY_ID_MAX] = '\0';
    clean.timezone[PANEL_TIMEZONE_MAX] = '\0';

    if (clean.theme > 3) {
        clean.theme = 0;
    }
    if (clean.backlight_percent > 100) {
        clean.backlight_percent = 100;
    }

    nvs_handle_t nvs;
    err = nvs_open(NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

#define SAVE_STR(key, field) do { \
        err = nvs_set_str(nvs, key, clean.field); \
        if (err != ESP_OK) goto out; \
    } while (0)

    SAVE_STR("wifi_ssid", wifi_ssid);
    SAVE_STR("hostname", hostname);
    SAVE_STR("admin_pass", admin_password);
    SAVE_STR("wifi_pass", wifi_password);
    SAVE_STR("mqtt_uri", mqtt_uri);
    SAVE_STR("mqtt_user", mqtt_username);
    SAVE_STR("mqtt_pass", mqtt_password);
    SAVE_STR("mqtt_topic", mqtt_base_topic);
    SAVE_STR("mqtt_disc", mqtt_discovery_prefix);
    SAVE_STR("panel_id", panel_id);
    SAVE_STR("display_title", display_title);
    SAVE_STR("ha_tapo", ha_tapo_entity);
    SAVE_STR("ha_over", ha_overhead_entity);
    SAVE_STR("ha_fan", ha_fan_entity);
    SAVE_STR("ha_echo", ha_echo_entity);
    SAVE_STR("timezone", timezone);

    SAVE_STR("wx_station", weather_station);
    SAVE_STR("wx_api", weather_api_key);
    SAVE_STR("wx_app", weather_app_key);
    SAVE_STR("wx_fc_key", weather_forecast_key);

#undef SAVE_STR

    err = nvs_set_u8(nvs, "prs_enabled", clean.presence_enabled);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "prs_address", clean.presence_address);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "prs_max_m", clean.presence_max_m);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "prs_trig_sens", clean.presence_trigger_sensitivity);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "prs_keep_sens", clean.presence_keep_sensitivity);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "prs_hold_s", clean.presence_hold_seconds);
    if (err != ESP_OK) goto out;

    err = nvs_set_u8(nvs, "als_enabled", clean.ambient_light_enabled);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "sensor_interval", clean.sensor_interval_seconds);
    if (err != ESP_OK) goto out;

    err = nvs_set_u8(nvs, "sleep_on", clean.sleep_enabled);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "sleep_delay", clean.sleep_delay_seconds);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "wake_minutes", clean.touch_wake_minutes);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "auto_bright", clean.auto_brightness);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "bright_min", clean.brightness_min);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "bright_max", clean.brightness_max);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "weather_on", clean.weather_enabled);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "wx_provider", clean.weather_provider);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "wx_forecast", clean.weather_forecast);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "wx_units", clean.weather_units);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "wx_poll", clean.weather_poll_minutes);
    if (err != ESP_OK) goto out;
    err = nvs_set_u8(nvs, "theme", clean.theme);
    if (err != ESP_OK) goto out;

    err = nvs_set_u8(nvs, "backlight", clean.backlight_percent);
    if (err != ESP_OK) goto out;

    for (int i = 0; i < PANEL_COLOR_PRESET_COUNT; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "color_%d", i);
        err = nvs_set_u32(nvs, key, clean.color_presets[i]);
        if (err != ESP_OK) goto out;
    }

    err = nvs_commit(nvs);
    if (err == ESP_OK) {
        *s_settings = clean;
        s_initialized = true;
    }

out:
    nvs_close(nvs);
    return err;
}

esp_err_t settings_reset(void)
{
    esp_err_t err = ensure_storage();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t nvs;
    err = nvs_open(NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        load_defaults(s_settings);
        s_initialized = true;
    }

    return err;
}


