#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mqtt_bridge_start(void);
void mqtt_bridge_poll(void);

void mqtt_bridge_tapo_power(bool on);
void mqtt_bridge_tapo_brightness(uint8_t percent);
void mqtt_bridge_tapo_rgb(uint8_t r, uint8_t g, uint8_t b);

void mqtt_bridge_overhead_power(bool on);
void mqtt_bridge_overhead_brightness(uint8_t percent);

void mqtt_bridge_fan_power(bool on);

void mqtt_bridge_echo_volume(uint8_t percent);
void mqtt_bridge_echo_mute(bool muted);
void mqtt_bridge_echo_play_pause(void);
void mqtt_bridge_echo_announce(void);

#ifdef __cplusplus
}
#endif
