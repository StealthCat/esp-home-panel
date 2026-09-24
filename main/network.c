#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_check.h"\n#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "settings.h"

static const char *TAG = "network";

#define WIFI_CONNECTED_BIT BIT0
#define CONFIG_AP_PASSWORD "configureme"

static EventGroupHandle_t s_wifi_events;
static bool s_connected;
static bool s_config_ap_active;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static char s_sta_ip[16];
static char s_config_ap_ssid[33];

static void start_sntp(void)
{
    const panel_settings_t *settings = settings_get();

    setenv("TZ", settings->timezone, 1);
    tzset();

    if (esp_sntp_enabled()) {
        return;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_init();
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const panel_settings_t *settings = settings_get();

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (settings->wifi_ssid[0] != '\0') {
            esp_wifi_connect();
        }
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_sta_ip[0] = '\0';
        if (s_wifi_events) {
            xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
        if (settings->wifi_ssid[0] != '\0') {
            ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
            esp_wifi_connect();
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        s_connected = true;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
        if (s_wifi_events) {
            xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
        ESP_LOGI(TAG, "Wi-Fi connected: %s", s_sta_ip);
        start_sntp();
    }
}

static void build_config_ap_ssid(void)
{
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        snprintf(s_config_ap_ssid, sizeof(s_config_ap_ssid),
                 "ESP-Home-Panel-%02X%02X%02X", mac[3], mac[4], mac[5]);
    } else {
        strlcpy(s_config_ap_ssid, "ESP-Home-Panel", sizeof(s_config_ap_ssid));
    }
}

static esp_err_t configure_config_ap(bool keep_station)
{
    build_config_ap_ssid();

    wifi_config_t ap_cfg = {0};
    strlcpy((char *)ap_cfg.ap.ssid, s_config_ap_ssid, sizeof(ap_cfg.ap.ssid));
    strlcpy((char *)ap_cfg.ap.password, CONFIG_AP_PASSWORD, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.ssid_len = strlen(s_config_ap_ssid);
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_mode(keep_station ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) {
        return err;
    }

    s_config_ap_active = true;
    ESP_LOGW(TAG,
             "Configuration AP enabled: SSID '%s', password '%s', web UI http://192.168.4.1/",
             s_config_ap_ssid, CONFIG_AP_PASSWORD);
    return ESP_OK;
}

esp_err_t network_start(void)
{
    const panel_settings_t *settings = settings_get();

    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_sta_netif || !s_ap_netif) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    if (settings->wifi_ssid[0] == '\0') {
        ESP_RETURN_ON_ERROR(configure_config_ap(false), TAG, "Setup AP configuration failed");
        return esp_wifi_start();
    }

    wifi_config_t sta_cfg = {0};
    strlcpy((char *)sta_cfg.sta.ssid, settings->wifi_ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, settings->wifi_password, sizeof(sta_cfg.sta.password));
    sta_cfg.sta.threshold.authmode =
        settings->wifi_password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Waiting for Wi-Fi SSID '%s'...", settings->wifi_ssid);
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(12000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "Initial Wi-Fi connection timed out; enabling setup AP while reconnect continues");
        return configure_config_ap(true);
    }

    return ESP_OK;
}

bool network_is_connected(void)
{
    return s_connected;
}

bool network_config_ap_active(void)
{
    return s_config_ap_active;
}

const char *network_get_sta_ip(void)
{
    return s_sta_ip[0] ? s_sta_ip : NULL;
}

const char *network_get_config_ap_ssid(void)
{
    return s_config_ap_active ? s_config_ap_ssid : NULL;
}

const char *network_get_config_ap_password(void)
{
    return s_config_ap_active ? CONFIG_AP_PASSWORD : NULL;
}
