#include "mqtt_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
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
static char s_client_id[64];

static char *s_topic_state_tapo;
static char *s_topic_state_overhead;
static char *s_topic_state_fan;
static char *s_topic_state_echo;

static char *s_topic_cmd_tapo;
static char *s_topic_cmd_overhead;
static char *s_topic_cmd_fan;
static char *s_topic_cmd_echo;

static char *s_topic_root;
static char *s_topic_availability;
static char *s_topic_request_state;
static char *s_topic_refresh_command;

static char *s_topic_ha_status;
static char *s_discovery_connection_config;
static char *s_discovery_refresh_config;
static char *s_discovery_tapo_event_config;
static char *s_discovery_overhead_event_config;
static char *s_discovery_fan_event_config;
static char *s_discovery_echo_event_config;

typedef enum {
    CONTROL_TAPO = 0,
    CONTROL_OVERHEAD,
    CONTROL_FAN,
    CONTROL_ALEXA,
    CONTROL_COUNT,
} control_id_t;

#define STATE_HOLDOFF_US      (900LL * 1000LL)
#define STATE_REFRESH_DELAY_US (1200LL * 1000LL)

static int64_t s_state_hold_until_us[CONTROL_COUNT];
static int64_t s_state_refresh_due_us;

static void note_local_command(control_id_t control)
{
    if (!s_connected || control < 0 || control >= CONTROL_COUNT) {
        return;
    }

    int64_t now = esp_timer_get_time();
    s_state_hold_until_us[control] = now + STATE_HOLDOFF_US;
    s_state_refresh_due_us = now + STATE_REFRESH_DELAY_US;

    ESP_LOGI(TAG,
             "MQTT optimistic hold control=%d for %lld ms; refresh due in %lld ms",
             (int)control,
             STATE_HOLDOFF_US / 1000LL,
             STATE_REFRESH_DELAY_US / 1000LL);
}

static bool state_update_is_held(control_id_t control, const char *topic)
{
    if (control < 0 || control >= CONTROL_COUNT) {
        return false;
    }

    int64_t now = esp_timer_get_time();
    if (now >= s_state_hold_until_us[control]) {
        return false;
    }

    ESP_LOGI(TAG,
             "MQTT state deferred during optimistic hold control=%d topic=%s remaining=%lld ms",
             (int)control,
             topic ? topic : "(unknown)",
             (s_state_hold_until_us[control] - now) / 1000LL);
    return true;
}

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

static char *alloc_string(size_t len)
{
    char *value = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!value) {
        value = malloc(len);
    }
    return value;
}

static char *make_topic(const char *base, const char *suffix)
{
    size_t len = strlen(base) + strlen(suffix) + 1;
    char *topic = alloc_string(len);
    if (!topic) {
        return NULL;
    }

    snprintf(topic, len, "%s%s", base, suffix);
    return topic;
}

static char *make_discovery_topic(const char *prefix,
                                  const char *component,
                                  const char *panel_id,
                                  const char *suffix)
{
    const char *object_prefix = "esp_home_panel_";
    size_t len = strlen(prefix) + 1 + strlen(component) + 1 +
                 strlen(object_prefix) + strlen(panel_id) + 1 +
                 strlen(suffix) + strlen("/config") + 1;

    char *topic = alloc_string(len);
    if (!topic) {
        return NULL;
    }

    snprintf(topic, len, "%s/%s/%s%s_%s/config",
             prefix, component, object_prefix, panel_id, suffix);
    return topic;
}

static esp_err_t build_topics(const panel_settings_t *settings)
{
    const char *base = settings->mqtt_base_topic;
    const char *panel_id = settings->panel_id;
    const char *discovery = settings->mqtt_discovery_prefix;

    size_t root_len = strlen(base) + 1 + strlen(panel_id) + 1;
    s_topic_root = alloc_string(root_len);
    if (!s_topic_root) {
        return ESP_ERR_NO_MEM;
    }

    snprintf(s_topic_root, root_len, "%s/%s", base, panel_id);

#define MAKE_TOPIC(dst, suffix) do { \
        dst = make_topic(s_topic_root, suffix); \
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
    MAKE_TOPIC(s_topic_refresh_command, "/control/refresh");

#undef MAKE_TOPIC

    s_topic_ha_status = make_topic(discovery, "/status");
    if (!s_topic_ha_status) return ESP_ERR_NO_MEM;

    s_discovery_connection_config =
        make_discovery_topic(discovery, "binary_sensor", panel_id, "connection");
    s_discovery_refresh_config =
        make_discovery_topic(discovery, "button", panel_id, "refresh");
    s_discovery_tapo_event_config =
        make_discovery_topic(discovery, "event", panel_id, "tapo");
    s_discovery_overhead_event_config =
        make_discovery_topic(discovery, "event", panel_id, "overhead");
    s_discovery_fan_event_config =
        make_discovery_topic(discovery, "event", panel_id, "fan");
    s_discovery_echo_event_config =
        make_discovery_topic(discovery, "event", panel_id, "echo");

    if (!s_discovery_connection_config ||
        !s_discovery_refresh_config ||
        !s_discovery_tapo_event_config ||
        !s_discovery_overhead_event_config ||
        !s_discovery_fan_event_config ||
        !s_discovery_echo_event_config) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static int mqtt_publish_logged(const char *topic,
                               const char *payload,
                               int qos,
                               int retain)
{
    if (!s_client || !topic) {
        ESP_LOGE(TAG, "MQTT TX refused: client/topic unavailable");
        return -1;
    }

    const char *safe_payload = payload ? payload : "";
    int msg_id = esp_mqtt_client_publish(
        s_client, topic, safe_payload, 0, qos, retain);

    ESP_LOGI(TAG,
             "MQTT TX PUBLISH msg_id=%d qos=%d retain=%d topic=\"%s\" payload=%s",
             msg_id, qos, retain, topic, safe_payload);
    return msg_id;
}

static int mqtt_subscribe_logged(const char *topic, int qos)
{
    if (!s_client || !topic) {
        ESP_LOGE(TAG, "MQTT SUBSCRIBE refused: client/topic unavailable");
        return -1;
    }

    int msg_id = esp_mqtt_client_subscribe(s_client, topic, qos);
    ESP_LOGI(TAG, "MQTT TX SUBSCRIBE msg_id=%d qos=%d topic=\"%s\"",
             msg_id, qos, topic);
    return msg_id;
}

static bool topic_equals(const esp_mqtt_event_handle_t event, const char *expected)
{
    if (!event || !event->topic || !expected) return false;
    size_t len = strlen(expected);
    return event->topic_len == (int)len && memcmp(event->topic, expected, len) == 0;
}

static bool payload_equals(const esp_mqtt_event_handle_t event, const char *expected)
{
    if (!event || !event->data || !expected) return false;
    size_t len = strlen(expected);
    return event->data_len == (int)len && memcmp(event->data, expected, len) == 0;
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

static const char *json_string(cJSON *root, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(item) || !item->valuestring) {
        return "";
    }
    return item->valuestring;
}

static void handle_state_message(esp_mqtt_event_handle_t event)
{
    cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
    if (!root) {
        ESP_LOGW(TAG, "MQTT state JSON invalid on topic=%.*s payload=%.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        return;
    }

    if (topic_equals(event, s_topic_state_tapo)) {
        if (state_update_is_held(CONTROL_TAPO, s_topic_state_tapo)) {
            cJSON_Delete(root);
            return;
        }
        bool on = json_bool(root, "on", false);
        uint8_t brightness = clamp_pct(json_int(root, "brightness", 0));
        uint8_t r = clamp_u8(json_int(root, "r", 255));
        uint8_t g = clamp_u8(json_int(root, "g", 255));
        uint8_t b = clamp_u8(json_int(root, "b", 255));
        ESP_LOGI(TAG, "MQTT APPLY tapo on=%d brightness=%u rgb=%u,%u,%u",
                 on, brightness, r, g, b);
        ui_apply_tapo_state(on, brightness, r, g, b);
    } else if (topic_equals(event, s_topic_state_overhead)) {
        if (state_update_is_held(CONTROL_OVERHEAD, s_topic_state_overhead)) {
            cJSON_Delete(root);
            return;
        }
        bool on = json_bool(root, "on", false);
        uint8_t brightness = clamp_pct(json_int(root, "brightness", 0));
        ESP_LOGI(TAG, "MQTT APPLY overhead on=%d brightness=%u", on, brightness);
        ui_apply_overhead_state(on, brightness);
    } else if (topic_equals(event, s_topic_state_fan)) {
        if (state_update_is_held(CONTROL_FAN, s_topic_state_fan)) {
            cJSON_Delete(root);
            return;
        }
        bool on = json_bool(root, "on", false);
        ESP_LOGI(TAG, "MQTT APPLY fan on=%d", on);
        ui_apply_fan_state(on);
    } else if (topic_equals(event, s_topic_state_echo)) {
        if (state_update_is_held(CONTROL_ALEXA, s_topic_state_echo)) {
            cJSON_Delete(root);
            return;
        }
        uint8_t volume = clamp_pct(json_int(root, "volume", 0));
        bool muted = json_bool(root, "muted", false);
        bool playing = json_bool(root, "playing", false);
        const char *title = json_string(root, "title");
        const char *artist = json_string(root, "artist");
        if (!artist[0]) artist = json_string(root, "author");
        ESP_LOGI(TAG,
                 "MQTT APPLY alexa volume=%u muted=%d playing=%d title=\"%s\" artist=\"%s\"",
                 volume, muted, playing, title, artist);
        ui_apply_echo_state(volume, muted, playing, title, artist);
    }

    cJSON_Delete(root);
}

static void add_device_info(cJSON *root, const panel_settings_t *settings)
{
    cJSON *device = cJSON_AddObjectToObject(root, "device");
    if (!device) return;

    char identifier[80];
    char name[96];
    snprintf(identifier, sizeof(identifier), "esp_home_panel_%s", settings->panel_id);
    snprintf(name, sizeof(name), "ESP Home Panel %s", settings->panel_id);

    cJSON *identifiers = cJSON_AddArrayToObject(device, "identifiers");
    if (identifiers) {
        cJSON_AddItemToArray(identifiers, cJSON_CreateString(identifier));
    }

    cJSON_AddStringToObject(device, "name", name);
    cJSON_AddStringToObject(device, "manufacturer", "StealthCat");
    cJSON_AddStringToObject(device, "model", "ESP32-S3 Touch LCD Home Panel");
}

static void add_unique_id(cJSON *root,
                          const panel_settings_t *settings,
                          const char *suffix)
{
    char unique_id[96];
    snprintf(unique_id, sizeof(unique_id),
             "esp_home_panel_%s_%s", settings->panel_id, suffix);
    cJSON_AddStringToObject(root, "unique_id", unique_id);
}

static void publish_discovery_json(const char *topic, cJSON *root)
{
    char *payload = cJSON_PrintUnformatted(root);
    if (!payload) {
        ESP_LOGE(TAG, "Unable to encode Home Assistant discovery payload for %s", topic);
        return;
    }

    mqtt_publish_logged(topic, payload, 1, 1);
    free(payload);
}

static void publish_home_assistant_discovery(void)
{
    if (!s_connected) return;

    const panel_settings_t *settings = settings_get();
    if (!settings || settings->mqtt_discovery_prefix[0] == '\0') {
        return;
    }

    ESP_LOGI(TAG,
             "Publishing Home Assistant MQTT discovery prefix=\"%s\" runtime_root=\"%s\"",
             settings->mqtt_discovery_prefix, s_topic_root);

    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "name", "Connection");
        add_unique_id(root, settings, "connection");
        cJSON_AddStringToObject(root, "state_topic", s_topic_availability);
        cJSON_AddStringToObject(root, "payload_on", "online");
        cJSON_AddStringToObject(root, "payload_off", "offline");
        cJSON_AddStringToObject(root, "device_class", "connectivity");
        cJSON_AddStringToObject(root, "entity_category", "diagnostic");
        add_device_info(root, settings);
        publish_discovery_json(s_discovery_connection_config, root);
        cJSON_Delete(root);
    }

    root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "name", "Refresh Home Assistant State");
        add_unique_id(root, settings, "refresh");
        cJSON_AddStringToObject(root, "command_topic", s_topic_refresh_command);
        cJSON_AddStringToObject(root, "payload_press", "refresh");
        cJSON_AddStringToObject(root, "availability_topic", s_topic_availability);
        cJSON_AddStringToObject(root, "payload_available", "online");
        cJSON_AddStringToObject(root, "payload_not_available", "offline");
        cJSON_AddStringToObject(root, "entity_category", "diagnostic");
        add_device_info(root, settings);
        publish_discovery_json(s_discovery_refresh_config, root);
        cJSON_Delete(root);
    }

    struct event_discovery {
        const char *topic;
        const char *name;
        const char *suffix;
        const char *state_topic;
        const char *types[6];
        size_t type_count;
    };

    const struct event_discovery events[] = {
        {
            .topic = s_discovery_tapo_event_config,
            .name = "Tapo Control",
            .suffix = "tapo",
            .state_topic = s_topic_cmd_tapo,
            .types = {"power", "brightness", "color"},
            .type_count = 3,
        },
        {
            .topic = s_discovery_overhead_event_config,
            .name = "Overhead Light Control",
            .suffix = "overhead",
            .state_topic = s_topic_cmd_overhead,
            .types = {"power", "brightness"},
            .type_count = 2,
        },
        {
            .topic = s_discovery_fan_event_config,
            .name = "Fan Control",
            .suffix = "fan",
            .state_topic = s_topic_cmd_fan,
            .types = {"power"},
            .type_count = 1,
        },
        {
            .topic = s_discovery_echo_event_config,
            .name = "Echo Control",
            .suffix = "echo",
            .state_topic = s_topic_cmd_echo,
            .types = {"volume", "mute", "play_pause", "previous_track", "next_track", "announce"},
            .type_count = 6,
        },
    };

    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i) {
        root = cJSON_CreateObject();
        if (!root) continue;

        cJSON_AddStringToObject(root, "name", events[i].name);
        add_unique_id(root, settings, events[i].suffix);
        cJSON_AddStringToObject(root, "state_topic", events[i].state_topic);
        cJSON_AddStringToObject(root, "availability_topic", s_topic_availability);
        cJSON_AddStringToObject(root, "payload_available", "online");
        cJSON_AddStringToObject(root, "payload_not_available", "offline");

        cJSON *types = cJSON_AddArrayToObject(root, "event_types");
        if (types) {
            for (size_t j = 0; j < events[i].type_count; ++j) {
                cJSON_AddItemToArray(types, cJSON_CreateString(events[i].types[j]));
            }
        }

        add_device_info(root, settings);
        publish_discovery_json(events[i].topic, root);
        cJSON_Delete(root);
    }
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
    cJSON_AddStringToObject(root, "panel_id", settings->panel_id);
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

    mqtt_publish_logged(s_topic_request_state, payload, 1, 0);
    free(payload);
    s_last_state_request_us = esp_timer_get_time();
}

static void log_rx_event(esp_mqtt_event_handle_t event)
{
    const char *topic = (event && event->topic) ? event->topic : "";
    int topic_len = (event && event->topic) ? event->topic_len : 0;
    const char *data = (event && event->data) ? event->data : "";
    int data_len = (event && event->data) ? event->data_len : 0;

    ESP_LOGI(TAG,
             "MQTT RX DATA msg_id=%d qos=%d retain=%d dup=%d offset=%d total=%d topic=\"%.*s\" payload=%.*s",
             event ? event->msg_id : -1,
             event ? event->qos : -1,
             event ? event->retain : 0,
             event ? event->dup : 0,
             event ? event->current_data_offset : 0,
             event ? event->total_data_len : 0,
             topic_len, topic,
             data_len, data);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "MQTT EVENT before_connect");
        break;

    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ui_set_mqtt_online(true);
        ESP_LOGI(TAG, "MQTT EVENT connected session_present=%d client_id=\"%s\"",
                 event ? event->session_present : 0, s_client_id);

        mqtt_subscribe_logged(s_topic_state_tapo, 1);
        mqtt_subscribe_logged(s_topic_state_overhead, 1);
        mqtt_subscribe_logged(s_topic_state_fan, 1);
        mqtt_subscribe_logged(s_topic_state_echo, 1);
        mqtt_subscribe_logged(s_topic_ha_status, 1);
        mqtt_subscribe_logged(s_topic_refresh_command, 1);

        mqtt_publish_logged(s_topic_availability, "online", 1, 1);
        publish_home_assistant_discovery();
        publish_state_request();
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ui_set_mqtt_online(false);
        ESP_LOGW(TAG, "MQTT EVENT disconnected");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT EVENT subscribed msg_id=%d", event ? event->msg_id : -1);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT EVENT unsubscribed msg_id=%d", event ? event->msg_id : -1);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT EVENT published/acked msg_id=%d", event ? event->msg_id : -1);
        break;

    case MQTT_EVENT_DATA:
        log_rx_event(event);

        if (topic_equals(event, s_topic_ha_status)) {
            if (payload_equals(event, "online")) {
                ESP_LOGI(TAG, "Home Assistant MQTT birth received; republishing discovery");
                publish_home_assistant_discovery();
                publish_state_request();
            }
        } else if (topic_equals(event, s_topic_refresh_command)) {
            ESP_LOGI(TAG, "Home Assistant refresh button requested state");
            publish_state_request();
        } else {
            handle_state_message(event);
        }
        break;

    case MQTT_EVENT_ERROR:
        if (event && event->error_handle) {
            ESP_LOGE(TAG,
                     "MQTT EVENT error type=%d connect_return_code=%d sock_errno=%d",
                     (int)event->error_handle->error_type,
                     (int)event->error_handle->connect_return_code,
                     event->error_handle->esp_transport_sock_errno);
        } else {
            ESP_LOGE(TAG, "MQTT EVENT error without detail");
        }
        break;

#ifdef MQTT_EVENT_DELETED
    case MQTT_EVENT_DELETED:
        ESP_LOGW(TAG, "MQTT EVENT outbox message deleted msg_id=%d",
                 event ? event->msg_id : -1);
        break;
#endif

    default:
        ESP_LOGD(TAG, "MQTT EVENT id=%ld msg_id=%d",
                 (long)event_id, event ? event->msg_id : -1);
        break;
    }
}

static void publish_json(const char *topic, const char *json)
{
    if (!s_client || !s_connected) {
        ESP_LOGW(TAG, "MQTT command dropped while disconnected topic=%s payload=%s",
                 topic ? topic : "(null)", json ? json : "");
        return;
    }

    mqtt_publish_logged(topic, json, 1, 0);
}

static void publish_targeted_json(const char *topic,
                                  const char *entity_id,
                                  const char *script_entity_id,
                                  const char *json)
{
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
    free(payload);
}

esp_err_t mqtt_bridge_start(void)
{
    const panel_settings_t *settings = settings_get();
    if (!settings) {
        return ESP_ERR_INVALID_STATE;
    }

    if (settings->mqtt_uri[0] == '\0') {
        ESP_LOGW(TAG, "MQTT URI is empty; MQTT disabled");
        return ESP_ERR_INVALID_STATE;
    }

    if (settings->mqtt_base_topic[0] == '\0') {
        ESP_LOGE(TAG, "MQTT runtime base topic is empty; MQTT disabled");
        return ESP_ERR_INVALID_STATE;
    }

    if (settings->mqtt_discovery_prefix[0] == '\0') {
        ESP_LOGE(TAG, "Home Assistant MQTT discovery prefix is empty; MQTT disabled");
        return ESP_ERR_INVALID_STATE;
    }

    if (settings->panel_id[0] == '\0') {
        ESP_LOGE(TAG, "Panel ID is empty; MQTT disabled");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = build_topics(settings);
    if (err != ESP_OK) {
        return err;
    }

    snprintf(s_client_id, sizeof(s_client_id), "esp-home-panel-%s", settings->panel_id);

    ESP_LOGI(TAG, "MQTT configuration:");
    ESP_LOGI(TAG, "  broker URI          = %s", settings->mqtt_uri);
    ESP_LOGI(TAG, "  username            = %s",
             settings->mqtt_username[0] ? settings->mqtt_username : "(none)");
    ESP_LOGI(TAG, "  client ID           = %s", s_client_id);
    ESP_LOGI(TAG, "  runtime base topic  = %s", settings->mqtt_base_topic);
    ESP_LOGI(TAG, "  panel topic root    = %s", s_topic_root);
    ESP_LOGI(TAG, "  HA discovery prefix = %s", settings->mqtt_discovery_prefix);
    ESP_LOGI(TAG, "  HA birth topic      = %s", s_topic_ha_status);
    ESP_LOGI(TAG, "  availability topic  = %s", s_topic_availability);
    ESP_LOGI(TAG, "  NOTE: MQTT password is intentionally not logged");

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = settings->mqtt_uri,
        .credentials.username = settings->mqtt_username[0] ? settings->mqtt_username : NULL,
        .credentials.client_id = s_client_id,
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
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(
        esp_mqtt_client_register_event(
            s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL),
        TAG, "MQTT event registration failed");

    err = esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "esp_mqtt_client_start -> %s", esp_err_to_name(err));
    return err;
}

void mqtt_bridge_poll(void)
{
    if (!s_connected) {
        return;
    }

    const int64_t now = esp_timer_get_time();

    if (s_state_refresh_due_us > 0) {
        if (now >= s_state_refresh_due_us) {
            ESP_LOGI(TAG, "MQTT requesting post-command state reconciliation");
            s_state_refresh_due_us = 0;
            publish_state_request();
        }
        return;
    }

    if (s_last_state_request_us == 0 ||
        now - s_last_state_request_us >= 10LL * 1000LL * 1000LL) {
        publish_state_request();
    }
}

void mqtt_bridge_tapo_power(bool on)
{
    note_local_command(CONTROL_TAPO);
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_tapo,
                          settings ? settings->ha_tapo_entity : NULL,
                          NULL,
                          on ? "{\"event_type\":\"power\",\"on\":true}"
                             : "{\"event_type\":\"power\",\"on\":false}");
}

void mqtt_bridge_tapo_brightness(uint8_t percent)
{
    note_local_command(CONTROL_TAPO);
    char payload[72];
    snprintf(payload, sizeof(payload),
             "{\"event_type\":\"brightness\",\"brightness\":%u}",
             clamp_pct(percent));
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_tapo,
                          settings ? settings->ha_tapo_entity : NULL,
                          NULL,
                          payload);
}

void mqtt_bridge_tapo_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    note_local_command(CONTROL_TAPO);
    char payload[112];
    snprintf(payload, sizeof(payload),
             "{\"event_type\":\"color\",\"r\":%u,\"g\":%u,\"b\":%u}",
             r, g, b);
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_tapo,
                          settings ? settings->ha_tapo_entity : NULL,
                          NULL,
                          payload);
}

void mqtt_bridge_overhead_power(bool on)
{
    note_local_command(CONTROL_OVERHEAD);
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_overhead,
                          settings ? settings->ha_overhead_entity : NULL,
                          NULL,
                          on ? "{\"event_type\":\"power\",\"on\":true}"
                             : "{\"event_type\":\"power\",\"on\":false}");
}

void mqtt_bridge_overhead_brightness(uint8_t percent)
{
    note_local_command(CONTROL_OVERHEAD);
    char payload[72];
    snprintf(payload, sizeof(payload),
             "{\"event_type\":\"brightness\",\"brightness\":%u}",
             clamp_pct(percent));
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_overhead,
                          settings ? settings->ha_overhead_entity : NULL,
                          NULL,
                          payload);
}

void mqtt_bridge_fan_power(bool on)
{
    note_local_command(CONTROL_FAN);
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_fan,
                          settings ? settings->ha_fan_entity : NULL,
                          NULL,
                          on ? "{\"event_type\":\"power\",\"on\":true}"
                             : "{\"event_type\":\"power\",\"on\":false}");
}

void mqtt_bridge_echo_volume(uint8_t percent)
{
    note_local_command(CONTROL_ALEXA);
    char payload[64];
    snprintf(payload, sizeof(payload),
             "{\"event_type\":\"volume\",\"volume\":%u}",
             clamp_pct(percent));
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_echo,
                          settings ? settings->ha_echo_entity : NULL,
                          NULL,
                          payload);
}

void mqtt_bridge_echo_mute(bool muted, uint8_t target_volume)
{
    note_local_command(CONTROL_ALEXA);

    char payload[112];
    snprintf(payload, sizeof(payload),
             "{\"event_type\":\"mute\",\"muted\":%s,\"volume\":%u}",
             muted ? "true" : "false",
             clamp_pct(target_volume));

    const panel_settings_t *settings = settings_get();
    publish_targeted_json(s_topic_cmd_echo,
                          settings ? settings->ha_echo_entity : NULL,
                          NULL,
                          payload);
}

void mqtt_bridge_echo_play_pause(bool playing)
{
    note_local_command(CONTROL_ALEXA);

    const panel_settings_t *settings = settings_get();
    publish_targeted_json(
        s_topic_cmd_echo,
        settings ? settings->ha_echo_entity : NULL,
        NULL,
        playing
            ? "{\"event_type\":\"play_pause\",\"playing\":true}"
            : "{\"event_type\":\"play_pause\",\"playing\":false}");
}

void mqtt_bridge_echo_track(bool next)
{
    note_local_command(CONTROL_ALEXA);
    const panel_settings_t *settings = settings_get();
    publish_targeted_json(
        s_topic_cmd_echo,
        settings ? settings->ha_echo_entity : NULL,
        NULL,
        next ? "{\"event_type\":\"next_track\"}"
             : "{\"event_type\":\"previous_track\"}");
}

void mqtt_bridge_echo_announce(void)
{
    note_local_command(CONTROL_ALEXA);
    const panel_settings_t *settings = settings_get();

    if (!settings || settings->ha_echo_announce_script[0] == '\0') {
        ESP_LOGW(TAG,
                 "Alexa announce pressed but no Home Assistant announcement script is configured");
    }

    publish_targeted_json(s_topic_cmd_echo,
                          settings ? settings->ha_echo_entity : NULL,
                          settings ? settings->ha_echo_announce_script : NULL,
                          "{\"event_type\":\"announce\",\"action\":\"announce\"}");
}

