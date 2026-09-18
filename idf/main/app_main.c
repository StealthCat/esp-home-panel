#include "board_7c.h"
#include "mqtt_bridge.h"
#include "network.h"
#include "ui.h"

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
        esp_err_t mqtt_err = mqtt_bridge_start();
        if (mqtt_err != ESP_OK) {
            ESP_LOGE(TAG, "MQTT startup failed: %s", esp_err_to_name(mqtt_err));
        }
    }

    while (true) {
        ui_set_network_online(network_is_connected());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
