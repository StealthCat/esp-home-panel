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

/* Call periodically from the task that sets the backlight. Detects a 7C-BOX IO
 * expander that was reset (e.g. by an accessory hot-plug dipping 3.3 V), which
 * leaves the backlight off, and restores its state. */
void board_7c_service(void);

#ifdef __cplusplus
}
#endif

