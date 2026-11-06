#!/usr/bin/env python3
"""Compile the production touch guard against a fake touch controller.

esp_lvgl_port passes touch read results to ESP_ERROR_CHECK(), so the guard must
turn shared-bus I2C failures into "no touch" instead of an abort.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_STATE 0x103
typedef int portMUX_TYPE;
static int lock_depth;
#define portENTER_CRITICAL(x) ((void)(x), lock_depth++)
#define portEXIT_CRITICAL(x) ((void)(x), lock_depth--)
typedef struct esp_lcd_touch_s esp_lcd_touch_t;
typedef esp_lcd_touch_t *esp_lcd_touch_handle_t;
typedef struct {uint8_t points; portMUX_TYPE lock;} esp_lcd_touch_data_t;
struct esp_lcd_touch_s {
 esp_err_t (*read_data)(esp_lcd_touch_handle_t tp);
 esp_lcd_touch_data_t data;
};
static int64_t now;
static int warnings;
static char last_warning[256];
static int64_t esp_timer_get_time(void) {return now;}
static const char *esp_err_to_name(esp_err_t err) {
 return err == ESP_ERR_TIMEOUT ? "ESP_ERR_TIMEOUT" : "ESP_ERR_INVALID_STATE";
}
static void log_warning(const char *tag, const char *format, ...) {
 assert(tag);
 va_list args;
 va_start(args, format);
 vsnprintf(last_warning, sizeof(last_warning), format, args);
 va_end(args);
 warnings++;
}
#define ESP_LOGW(tag, format, ...) log_warning(tag, format, __VA_ARGS__)
'''
TEST = r'''
#include "touch_guard.c"
#include <string.h>
static esp_err_t result;
static int reads;
static esp_err_t fake_read(esp_lcd_touch_handle_t tp) {
 reads++;
 if (result == ESP_OK) tp->data.points = 2;
 return result;
}
int main(void) {
 touch_guard_install(NULL); /* no handle: must not crash */
 esp_lcd_touch_t uninitialized = {0};
 touch_guard_install(&uninitialized);
 assert(uninitialized.read_data == NULL);

 esp_lcd_touch_t touch = {.read_data = fake_read};
 touch_guard_install(&touch);
 assert(touch.read_data != fake_read);
 esp_err_t (*guarded)(esp_lcd_touch_handle_t) = touch.read_data;
 touch_guard_install(&touch); /* idempotent: never wraps itself */
 assert(touch.read_data == guarded);

 /* Successful reads pass through unchanged. */
 result = ESP_OK;
 assert(touch.read_data(&touch) == ESP_OK && touch.data.points == 2 && reads == 1);
 assert(warnings == 0);

 /* A failed read is not fatal, reports release and is logged. */
 result = ESP_ERR_TIMEOUT;
 assert(touch.read_data(&touch) == ESP_OK && reads == 2);
 assert(touch.data.points == 0 && lock_depth == 0 && warnings == 1);
 assert(strstr(last_warning, "Ignored 1 touch") && strstr(last_warning, "ESP_ERR_TIMEOUT"));

 /* Repeated failures are rate limited instead of flooding the log. */
 for (int i = 0; i < 50; i++) {
  now += 10000;
  touch.data.points = 3;
  assert(touch.read_data(&touch) == ESP_OK && touch.data.points == 0);
 }
 assert(warnings == 1 && s_unreported_failures == 50);
 now += 5000000;
 result = ESP_ERR_INVALID_STATE;
 assert(touch.read_data(&touch) == ESP_OK && warnings == 2 && s_unreported_failures == 0);
 assert(strstr(last_warning, "Ignored 51 touch") && strstr(last_warning, "ESP_ERR_INVALID_STATE"));

 /* Touch works again once the bus recovers. */
 result = ESP_OK;
 assert(touch.read_data(&touch) == ESP_OK && touch.data.points == 2);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d/'stub.h').write_text(STUB)
    for header in ['esp_lcd_touch.h', 'esp_log.h', 'esp_timer.h']:
        (d/header).write_text('#include "stub.h"\n')
    (d/'test.c').write_text(TEST)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(d), '-I'+str(ROOT/'main'),
                    str(d/'test.c'), '-o', str(d/'test')], check=True)
    subprocess.run([str(d/'test')], check=True)
print('Touch guard pass-through, non-fatal failure, release, rate-limit and recovery checks passed')
