#include "mqtt_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "settings.h"
#include "ui.h"

static const char *TAG = "mqtt_bridge";

static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static int64_t s_last_state_request_us;

static char *s_topic_state_tapo;
static char *s_topic_state_overhead;
static char *s_topic_state_fan;
static char *s_topic_state_echo;

static char *s_topic_cmd_tapo;
static char *s_topic_cmd_overhead;
static char *s_topic_cmd_fan;
static char *s_topic_cmd_echo;

static char *s_topic_availability;
static char *s_topic_request_state;

static uint8_t clamp_pct(int value)
{
    if (value < 0) return 0;
    if (value > 100) return 100;
    return (uint8_t)value;
}

static uint8_t clamp_u8(int value)
{
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

static char *make_topic(const char *base, const char *suffix)
{
    size_t len = strlen(base) + strlen(suffix) + 1;
    char *topic = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!topic) {
        topic = malloc(len);
    }
    if (!topic) {
        return NULL;
    }

    snprintf(topic, len, "%s%s", base, suffix);
    return topic;
}

static esp_err_t build_topics(const char *base)
{
#define MAKE_TOPIC(dst, suffix) do { \
        dst = make_topic(base, suffix); \
        if (!dst) return ESP_ERR_NO_MEM; \
    } while (0)

    MAKE_TOPIC(s_topic_state_tapo, "/state/tapo");
    MAKE_TOPIC(s_topic_state_overhead, "/state/overhead");
    MAKE_TOPIC(s_topic_state_fan, "/state/fan");
    MAKE_TOPIC(s_topic_state_echo, "/state/echo");

    MAKE_TOPIC(s_topic_cmd_tapo, "/cmd/tapo");
    MAKE_TOPIC(s_topic_cmd_overhead, "/cmd/overhead");
    MAKE_TOPIC(s_topic_cmd_fan, "/cmd/fan");
    MAKE_TOPIC(s_topic_cmd_echo, "/cmd/echo");

    MAKE_TOPIC(s_topic_availability, "/availability");
    MAKE_TOPIC(s_topic_request_state, "/request_state");

#undef MAKE_TOPIC
    return ESP_OK;
}

static bool topic_equals(const esp_mqtt_event_handle_t event, const char *expected)
{
    if (!expected) return false;
    size_t len = strlen(expected);
    return event->topic_len == (int)len && memcmp(event->topic, expected, len) == 0;
}

static bool json_bool(cJSON *root, const char *name, bool fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsBool(item)) return fallback;
    return cJSON_IsTrue(item);
}

static int json_int(cJSON *root, const char *name, int fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item)) return fallback;
    return item->valueint;
}

static void handle_state_message(esp_mqtt_event_handle_t event)
{
    cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
    if (!root) {
        ESP_LOGW(TAG, "Invalid state JSON on %.*s", event->topic_len, event->topic);
        return;
    }

    if (topic_equals(event, s_topic_state_tapo)) {
        bool on = json_bool(root, "on", false);
        uint8_t brightness = clamp_pct(json_int(root, "brightness", 0));
        uint8_t r = clamp_u8(json_int(root, "r", 255));
        uint8_t g = clamp_u8(json_int(root, "g", 255));
        uint8_t b = clamp_u8(json_int(root, "b", 255));
        ui_apply_tapo_state(on, brightness, r, g, b);
    } else if (topic_equals(event, s_topic_state_overhead)) {
        bool on = json_bool(root, "on", false);
        uint8_t brightness = clamp_pct(json_int(root, "brightness", 0));
        ui_apply_overhead_state(on, brightness);
    } else if (topic_equals(event, s_topic_state_fan)) {
        ui_apply_fan_state(json_bool(root, "on", false));
    } else if (topic_equals(event, s_topic_state_echo)) {
        uint8_t volume = clamp_pct(json_int(root, "volume", 0));
        bool muted = json_bool(root, "muted", false);
        bool playing = json_bool(root, "playing", false);
        ui_apply_echo_state(volume, muted, playing);
    }

    cJSON_Delete(root);
}

static void publish_state_request(void)
{
    if (!s_client || !s_connected || !s_topic_request_state) {
        return;
    }

    const panel_settings_t *settings = settings_get();
    if (!settings) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGW(TAG, "Unable to allocate Home Assistant state request");
        return;
    }

    cJSON_AddNumberToObject(root, "request", 1);
    cJSON_AddStringToObject(root, "tapo", settings->ha_tapo_entity);
    cJSON_AddStringToObject(root, "overhead", settings->ha_overhead_entity);
    cJSON_AddStringToObject(root, "fan", settings->ha_fan_entity);
    cJSON_AddStringToObject(root, "echo", settings->ha_echo_entity);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        ESP_LOGW(TAG, "Unable to encode Home Assistant state request");
        return;
    }

    esp_mqtt_client_publish(s_client, s_topic_request_state, payload, 0, 1, 0);
    cJSON_free(payload);
    s_last_state_request_us = esp_timer_get_time();
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ui_set_mqtt_online(true);
        ESP_LOGI(TAG, "MQTT connected");

        esp_mqtt_client_subscribe(s_client, s_topic_state_tapo, 1);
        esp_mqtt_client_subscribe(s_client, s_topic_state_overhead, 1);
        esp_mqtt_client_subscribe(s_client, s_topic_state_fan, 1);
        esp_mqtt_client_subscribe(s_client, s_topic_state_echo, 1);

        esp_mqtt_client_publish(s_client, s_topic_availability, "online", 0, 1, 1);
        publish_state_request();
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ui_set_mqtt_online(false);
        ESP_LOGW(TAG, "MQTT disconnected");
        break;

    case MQTT_EVENT_DATA:
        handle_state_message(event);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT transport error");
        break;

    default:
        break;
    }
}

static void publish_json(const char *topic, const char *json)
{
    if (!s_client || !s_connected) {
        ESP_LOGW(TAG, "MQTT command dropped while disconnected: %s", json);
        return;
    }

    esp_mqtt_client_publish(s_client, topic, json, 0, 1, 0);
}

static void publish_targeted_json(const char *topic,
                                  const char *entity_id,
                                  const char *script_entity_id,
                                  const char *json)
{
    if ((!entity_id || entity_id[0] == '\0') &&
        (!script_entity_id || script_entity_id[0] == '\0')) {
        publish_json(topic, json);
        return;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "Unable to parse command JSON: %s", json);
        return;
    }

    if (entity_id && entity_id[0] != '\0') {
        cJSON_AddStringToObject(root, "entity_id", entity_id);
    }
    if (script_entity_id && script_entity_id[0] != '\0') {
        cJSON_AddStringToObject(root, "script_entity_id", script_entity_id);
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        ESP_LOGW(TAG, "Unable to encode targeted MQTT command");
        return;
    }

    publish_json(topic, payload);
    cJSON_free(payload);
}

esp_err_t mqtt_bridge_start(void)
{
    const panel_settings_t *settings = settings_get();

    if (settings->mqtt_uri[0] == '\0') {
        ESP_LOGW(TAG, "MQTT URI is empty; MQTT disabled");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = build_topics(settings->mqtt_base_topic);
    if (err != ESP_OK) {
        return err;
    }

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = settings->mqtt_uri,
        .credentials.username = settings->mqtt_username[0] ? settings->mqtt_username : NULL,
        .credentials.authentication.password =
            settings->mqtt_password[0] ? settings->mqtt_password : NULL,
        .session.last_will = {
            .topic = s_topic_availability,
            .msg = "offline",
            .qos = 1,
            .retain = 1,
        },
        .network.reconnect_timeout_ms = 3000,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    return esp_mqtt_client_start(s_client);
}

void mqtt_bridge_poll(void)
{
    if (!s_connected) {
        return;
    }

    const int64_t now = esp_timer_get_time();
    if (s_last_state_request_us == 0 ||
        now - s_last_state_request_us >= 10LL * 1000LL * 1000LL) {
        publish_state_request();
    }
}

void mqtt_bridge_tapo_power(bool on)
{
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_tapo,
                          settings ? settings->ha_tapo_entity : NULL,
                          NULL,
                          on ? "{\"on\":true}" : "{\"on\":false}");
}

void mqtt_bridge_tapo_brightness(uint8_t percent)
{
    char payload[40];
    snprintf(payload, sizeof(payload), "{\"brightness\":%u}", clamp_pct(percent));
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_tapo,
                          settings ? settings->ha_tapo_entity : NULL,
                          NULL,
                          payload);
}

void mqtt_bridge_tapo_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    char payload[80];
    snprintf(payload, sizeof(payload), "{\"r\":%u,\"g\":%u,\"b\":%u}", r, g, b);
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_tapo,
                          settings ? settings->ha_tapo_entity : NULL,
                          NULL,
                          payload);
}

void mqtt_bridge_overhead_power(bool on)
{
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_overhead,
                          settings ? settings->ha_overhead_entity : NULL,
                          NULL,
                          on ? "{\"on\":true}" : "{\"on\":false}");
}

void mqtt_bridge_overhead_brightness(uint8_t percent)
{
    char payload[40];
    snprintf(payload, sizeof(payload), "{\"brightness\":%u}", clamp_pct(percent));
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_overhead,
                          settings ? settings->ha_overhead_entity : NULL,
                          NULL,
                          payload);
}

void mqtt_bridge_fan_power(bool on)
{
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_fan,
                          settings ? settings->ha_fan_entity : NULL,
                          NULL,
                          on ? "{\"on\":true}" : "{\"on\":false}");
}

void mqtt_bridge_echo_volume(uint8_t percent)
{
    char payload[40];
    snprintf(payload, sizeof(payload), "{\"volume\":%u}", clamp_pct(percent));
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_echo,
                          settings ? settings->ha_echo_entity : NULL,
                          NULL,
                          payload);
}

void mqtt_bridge_echo_mute(bool muted)
{
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_echo,
                          settings ? settings->ha_echo_entity : NULL,
                          NULL,
                          muted ? "{\"muted\":true}" : "{\"muted\":false}");
}

void mqtt_bridge_echo_play_pause(void)
{
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_echo,
                          settings ? settings->ha_echo_entity : NULL,
                          NULL,
                          "{\"action\":\"play_pause\"}");
}

void mqtt_bridge_echo_announce(void)
{
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_echo,
                          settings ? settings->ha_echo_entity : NULL,
                          settings ? settings->ha_echo_announce_script : NULL,
                          "{\"action\":\"announce\"}");
}
