# Reconfigure native ESP-IDF builds when an asset or its manifest changes.
file(GLOB THEME_ART_PNGS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/../assets/theme-sprites/*.png")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${THEME_ART_PNGS}
    "${CMAKE_CURRENT_LIST_DIR}/../assets/theme-sprites/manifest.json"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/generate_theme_art.py"
    "${CMAKE_CURRENT_LIST_DIR}/../tools/check_theme_assets.py"
    "${CMAKE_CURRENT_LIST_DIR}/ui.c"
    "${CMAKE_CURRENT_LIST_DIR}/theme_art.h")

# The theme sprites are generated source code. PlatformIO also runs the
# generator as a pre-build step, but native ESP-IDF/idf.py builds do not run
# PlatformIO extra_scripts. Generate them here as part of CMake configuration
# so every supported build path embeds the artwork instead of silently falling
# back to a sprite-less firmware.
if(DEFINED PYTHON)
    set(THEME_ART_PYTHON "${PYTHON}")
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(THEME_ART_PYTHON "${Python3_EXECUTABLE}")
endif()

execute_process(
    COMMAND "${THEME_ART_PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../tools/check_theme_assets.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
    RESULT_VARIABLE THEME_ART_RESULT
    OUTPUT_VARIABLE THEME_ART_STDOUT
    ERROR_VARIABLE THEME_ART_STDERR
)

if(NOT THEME_ART_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Failed to generate LVGL theme sprites.\n"
        "${THEME_ART_STDOUT}\n${THEME_ART_STDERR}")
endif()

message(STATUS "${THEME_ART_STDOUT}")

idf_component_register(
    SRCS
        "app_main.c"
        "board_7c.c"
        "network.c"
        "settings.c"
        "web_config.c"
        "mqtt_bridge.c"
        "ui.c"
        "ui_theme.c"
        "theme_art.c"
        "wokwi_panel.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        esp_hw_support
        esp_timer
        esp_http_server
        lwip
        nvs_flash
        mqtt
        json
        esp_lcd
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
)

