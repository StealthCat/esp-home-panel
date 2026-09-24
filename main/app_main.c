#include "board_7c.h"
#include "mqtt_bridge.h"
#include "network.h"
#include "settings.h"
#include "ui.h"
#include "web_config.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(settings_init());

    board_7c_handles_t board;
    ESP_ERROR_CHECK(board_7c_init(&board));

    /*
     * Bring up the UI before networking. This gives immediate visual feedback
     * even if Wi-Fi or the MQTT broker is unavailable.
     */
    ESP_ERROR_CHECK(ui_start(board.panel, board.touch));

    err = network_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Network startup failed: %s", esp_err_to_name(err));
    }

    ui_set_network_online(network_is_connected());

    if (err == ESP_OK) {
        esp_err_t web_err = web_config_start();
        if (web_err != ESP_OK) {
            ESP_LOGE(TAG, "Web configuration startup failed: %s", esp_err_to_name(web_err));
        }
    }

    bool mqtt_started = false;

    while (true) {
        bool online = network_is_connected();
        ui_set_network_online(online);

        if (online && !mqtt_started) {
            const panel_settings_t *settings = settings_get();
            if (settings && settings->mqtt_uri[0] != '\0') {
                esp_err_t mqtt_err = mqtt_bridge_start();
                if (mqtt_err == ESP_OK) {
                    mqtt_started = true;
                } else {
                    ESP_LOGE(TAG, "MQTT startup failed: %s", esp_err_to_name(mqtt_err));
                }
            } else {
                mqtt_started = true;
            }
        }

        if (mqtt_started) {
            mqtt_bridge_poll();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
