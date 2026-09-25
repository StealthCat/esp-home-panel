#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"
typedef struct {int task_priority,task_stack,task_affinity,task_max_sleep_ms,task_stack_caps,timer_period_ms;} lvgl_port_cfg_t;
static inline esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg) {(void)cfg;lv_init();return ESP_OK;}
static inline bool lvgl_port_lock(int timeout) {(void)timeout;return true;}
static inline void lvgl_port_unlock(void) {}
