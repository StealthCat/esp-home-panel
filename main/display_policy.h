#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "settings.h"

typedef struct {
    int64_t vacant_since_us, touch_until_us;
    bool vacant_tracking, asleep;
    float filtered_lux;
    bool have_lux;
    uint8_t brightness;
} display_policy_t;

void display_policy_touch(display_policy_t *state, const panel_settings_t *cfg, int64_t now);
uint8_t display_policy_step(display_policy_t *state, const panel_settings_t *cfg,
    int64_t now, bool presence_valid, bool occupied, bool lux_valid, float lux, bool pwm);
