#!/usr/bin/env python3
"""Exercise production sensor polling against a fake clock and I2C device."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>
typedef int esp_err_t;
#define ESP_OK 0
#define I2C_ADDR_BIT_LEN_7 0
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef void *esp_lcd_panel_handle_t;
typedef void *esp_lcd_touch_handle_t;
typedef struct {int dev_addr_length; int device_address; int scl_speed_hz;} i2c_device_config_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
static int64_t now;
static bool failed, no_bus;
static int writes, reads, adds;
static uint16_t raw;
static int64_t esp_timer_get_time(void) {return now;}
static int i2c_master_bus_add_device(void *b,const i2c_device_config_t *c,void **d) {
 assert(b && c->device_address==0x10 && c->scl_speed_hz==100000);*d=(void*)2;adds++;return 0;
}
static int i2c_master_transmit(void *d,const uint8_t *v,size_t n,int timeout) {
 assert(d && n==3 && timeout==20);writes++;
 assert((v[0]==3 && v[1]==0 && v[2]==0) || (v[0]==0 && v[1]==0 && v[2]==0x10));
 return failed ? -1:0;
}
static int i2c_master_transmit_receive(void *d,const uint8_t *v,size_t n,uint8_t *r,size_t m,int timeout) {
 assert(d && n==1 && *v==4 && m==2 && timeout==20);reads++;
 r[0]=raw&255;r[1]=raw>>8;return failed?-1:0;
}
'''
TEST = r'''
#include "sensors.c"
static panel_settings_t cfg = {.sensor_interval_seconds=1};
const panel_settings_t *settings_get(void) {return &cfg;}
i2c_master_bus_handle_t board_7c_i2c_bus(void) {return no_bus?NULL:(void*)1;}
int main(void) {
 sensors_poll();assert(adds==0); /* disabled by default */
 cfg.ambient_light_enabled=1;no_bus=true;sensors_poll();assert(adds==0);
 no_bus=false;now=1000000;failed=true;sensors_poll();assert(!sensors_ambient_light().available);
 failed=false;now=2000000;sensors_poll();assert(reads==0);
 now+=200000;sensors_poll();assert(reads==0); /* integration not complete */
 now+=50000;raw=100;sensors_poll();assert(reads==1);
 assert(sensors_ambient_light().available && fabsf(sensors_ambient_light().lux-53.76f)<0.01f);
 now+=1000000;raw=0;sensors_poll();assert(sensors_ambient_light().available && sensors_ambient_light().lux==0);
 now+=1000000;failed=true;sensors_poll();assert(!sensors_ambient_light().available);
 now+=1000000;failed=false;sensors_poll(); /* reconnect reconfigures */
 now+=250000;raw=65535;sensors_poll();assert(!sensors_ambient_light().available);
 now+=1000000;raw=3000;sensors_poll();assert(sensors_ambient_light().available);
 assert(sensors_ambient_light().lux>raw*0.5376f); /* high-light correction */
 assert(adds==1); /* reuse handle, do not leak on reconnect */
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d/'stub.h').write_text(STUB)
    for header in ['esp_err.h','esp_timer.h','esp_lcd_panel_ops.h','esp_lcd_touch.h','driver/i2c_master.h','freertos/FreeRTOS.h']:
        file=d/header
        file.parent.mkdir(parents=True,exist_ok=True)
        file.write_text('#include "stub.h"\n')
    (d/'test.c').write_text(TEST)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(d),'-I'+str(ROOT/'main'),str(d/'test.c'),'-lm','-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
print('Sensor startup, timing, conversion, disconnect, recovery and saturation checks passed')
