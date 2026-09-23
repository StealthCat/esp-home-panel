cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp_home_panel_7c)

# LVGL 9.5.0's SVG decoder logs a uint32_t with "%u". On the ESP32 toolchain
# uint32_t is typedef'd as unsigned long, so -Werror=format turns this upstream
# warning into a build failure. Keep format warnings enabled, but don't promote
# this warning to an error for the managed LVGL C sources only.
idf_component_get_property(lvgl_lib "lvgl__lvgl" COMPONENT_LIB)
target_compile_options(
    ${lvgl_lib}
    PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wno-error=format>
)
