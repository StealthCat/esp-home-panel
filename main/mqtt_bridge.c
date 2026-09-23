#include "mqtt_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "ui.h"

static const char *TAG = "mqtt_bridge";

static esp_mqtt_client_handle_t s_client;
static bool s_connected;

/*
 * MQTT topics are compile-time strings. Keeping them in read-only storage
 * avoids reserving ten 160-byte buffers in scarce internal DRAM (.bss).
 */
static const char s_topic_state_tapo[] = CONFIG_PANEL_MQTT_BASE_TOPIC "/state/tapo";
static const char s_topic_state_overhead[] = CONFIG_PANEL_MQTT_BASE_TOPIC "/state/overhead";
static const char s_topic_state_fan[] = CONFIG_PANEL_MQTT_BASE_TOPIC "/state/fan";
static const char s_topic_state_echo[] = CONFIG_PANEL_MQTT_BASE_TOPIC "/state/echo";

static const char s_topic_cmd_tapo[] = CONFIG_PANEL_MQTT_BASE_TOPIC "/cmd/tapo";
static const char s_topic_cmd_overhead[] = CONFIG_PANEL_MQTT_BASE_TOPIC "/cmd/overhead";
static const char s_topic_cmd_fan[] = CONFIG_PANEL_MQTT_BASE_TOPIC "/cmd/fan";
static const char s_topic_cmd_echo[] = CONFIG_PANEL_MQTT_BASE_TOPIC "/cmd/echo";

static const char s_topic_availability[] = CONFIG_PANEL_MQTT_BASE_TOPIC "/availability";
static const char s_topic_request_state[] = CONFIG_PANEL_MQTT_BASE_TOPIC "/request_state";

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

static bool topic_equals(const esp_mqtt_event_handle_t event, const char *expected)
{
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
        esp_mqtt_client_publish(s_client, s_topic_request_state, "1", 0, 1, 0);
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

esp_err_t mqtt_bridge_start(void)
{
    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_PANEL_MQTT_URI,
        .credentials.username = CONFIG_PANEL_MQTT_USERNAME[0] ? CONFIG_PANEL_MQTT_USERNAME : NULL,
        .credentials.authentication.password = CONFIG_PANEL_MQTT_PASSWORD[0] ? CONFIG_PANEL_MQTT_PASSWORD : NULL,
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

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    return esp_mqtt_client_start(s_client);
}

void mqtt_bridge_tapo_power(bool on)
{
    publish_json(s_topic_cmd_tapo, on ? "{\"on\":true}" : "{\"on\":false}");
}

void mqtt_bridge_tapo_brightness(uint8_t percent)
{
    char payload[40];
    snprintf(payload, sizeof(payload), "{\"brightness\":%u}", clamp_pct(percent));
    publish_json(s_topic_cmd_tapo, payload);
}

void mqtt_bridge_tapo_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    char payload[80];
    snprintf(payload, sizeof(payload), "{\"r\":%u,\"g\":%u,\"b\":%u}", r, g, b);
    publish_json(s_topic_cmd_tapo, payload);
}

void mqtt_bridge_overhead_power(bool on)
{
    publish_json(s_topic_cmd_overhead, on ? "{\"on\":true}" : "{\"on\":false}");
}

void mqtt_bridge_overhead_brightness(uint8_t percent)
{
    char payload[40];
    snprintf(payload, sizeof(payload), "{\"brightness\":%u}", clamp_pct(percent));
    publish_json(s_topic_cmd_overhead, payload);
}

void mqtt_bridge_fan_power(bool on)
{
    publish_json(s_topic_cmd_fan, on ? "{\"on\":true}" : "{\"on\":false}");
}

void mqtt_bridge_echo_volume(uint8_t percent)
{
    char payload[40];
    snprintf(payload, sizeof(payload), "{\"volume\":%u}", clamp_pct(percent));
    publish_json(s_topic_cmd_echo, payload);
}

void mqtt_bridge_echo_mute(bool muted)
{
    publish_json(s_topic_cmd_echo, muted ? "{\"muted\":true}" : "{\"muted\":false}");
}

void mqtt_bridge_echo_play_pause(void)
{
    publish_json(s_topic_cmd_echo, "{\"action\":\"play_pause\"}");
}

void mqtt_bridge_echo_announce(void)
{
    publish_json(s_topic_cmd_echo, "{\"action\":\"announce\"}");
}
