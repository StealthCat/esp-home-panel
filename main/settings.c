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

    uint8_t value = 0;
    if (nvs_get_u8(nvs, "theme", &value) == ESP_OK && value <= 3) {
        s_settings->theme = value;
    }
    if (nvs_get_u8(nvs, "backlight", &value) == ESP_OK && value <= 100) {
        s_settings->backlight_percent = value;
    }

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

esp_err_t settings_save(const panel_settings_t *settings)
{
    if (!settings) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_storage();
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < PANEL_COLOR_PRESET_COUNT; ++i) {
        if (settings->color_presets[i] > 0xFFFFFF) return ESP_ERR_INVALID_ARG;
    }

    panel_settings_t clean = *settings;
    clean.wifi_ssid[PANEL_WIFI_SSID_MAX] = '\0';
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

#undef SAVE_STR

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

