#pragma once

#ifdef WOKWI_EMULATOR

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wokwi_panel_init(void);
lv_display_t *wokwi_panel_create_display(void);
esp_err_t wokwi_panel_add_touch(lv_display_t *display);

#ifdef __cplusplus
}
#endif

#endif
