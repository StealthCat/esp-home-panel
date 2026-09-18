#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_start(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t touch);

void ui_set_network_online(bool online);
void ui_set_mqtt_online(bool online);

void ui_apply_tapo_state(bool on, uint8_t brightness, uint8_t r, uint8_t g, uint8_t b);
void ui_apply_overhead_state(bool on, uint8_t brightness);
void ui_apply_fan_state(bool on);
void ui_apply_echo_state(uint8_t volume, bool muted, bool playing);

#ifdef __cplusplus
}
#endif
