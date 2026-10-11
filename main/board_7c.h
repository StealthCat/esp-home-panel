#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PANEL_H_RES 800
#define PANEL_V_RES 480

typedef struct {
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
} board_7c_handles_t;

esp_err_t board_7c_init(board_7c_handles_t *handles);
esp_err_t board_7c_set_backlight(uint8_t percent);
bool board_7c_has_pwm_backlight(void);

#ifdef __cplusplus
}
#endif

