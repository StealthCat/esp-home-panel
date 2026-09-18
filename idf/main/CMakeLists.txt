idf_component_register(
    SRCS
        "app_main.c"
        "board_7c.c"
        "network.c"
        "mqtt_bridge.c"
        "ui.c"
        "ui_theme.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        nvs_flash
        mqtt
        json
        esp_lcd
)
