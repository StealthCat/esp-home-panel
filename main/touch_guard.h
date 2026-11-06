#pragma once
#include "esp_lcd_touch.h"

/*
 * Keep transient touch-controller read errors from rebooting the panel.
 *
 * esp_lvgl_port's touch read callback passes the controller result straight to
 * ESP_ERROR_CHECK(), so one NACK, timeout or bus-clear failure on the I2C bus
 * that touch shares with the IO expander and optional sensors aborts the whole
 * firmware. After installation a failed read is logged (rate limited) and
 * reported to LVGL as "no touch"; the next touch interrupt reads again.
 */
void touch_guard_install(esp_lcd_touch_handle_t touch);
