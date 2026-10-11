#!/usr/bin/env python3
"""Compile the production C4001 state machine with a register-level radar fake."""
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
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define I2C_ADDR_BIT_LEN_7 0
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct {int dev_addr_length; int device_address; int scl_speed_hz;} i2c_device_config_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
static int64_t now, ready_at;
static bool failed, no_bus, corrupt_config;
static int adds, saves, toggles, writes, expected_address=42;
static uint8_t registers[256];
static int64_t esp_timer_get_time(void) {return now;}
static int i2c_master_bus_add_device(void *b,const i2c_device_config_t *c,void **d) {
 assert(b && c->device_address==expected_address && c->scl_speed_hz==100000);
 *d=(void*)2;adds++;return 0;
}
static int i2c_master_transmit(void *d,const uint8_t *v,size_t n,int timeout) {
 assert(d && timeout==20 && now>=ready_at);writes++;
 if(failed)return -1;
 if(n==2 && v[0]==1){
   if(v[1]==0x33)registers[0]&=~1;
   else {assert(v[1]==0x55);registers[0]|=1;}
   ready_at=now+200000;
 } else if(n==2 && v[0]==2){
   if(v[1]==0x3b){toggles++;registers[0]^=2;ready_at=now+1500000;}
   else {assert(v[1]==0x5c);saves++;ready_at=now+500000;}
 } else {assert(v[0]==0x20 && n==12);if(!corrupt_config)memcpy(registers+0x20,v+1,11);}
 return 0;
}
static int i2c_master_transmit_receive(void *d,const uint8_t *v,size_t n,uint8_t *r,size_t m,int timeout) {
 assert(d && n==1 && timeout==20 && now>=ready_at);
 if(failed)return -1;
 memcpy(r,registers+*v,m);return 0;
}
'''
TEST = r'''
#include "c4001.c"
static panel_settings_t cfg={.presence_address=42,.presence_max_m=6,
 .presence_trigger_sensitivity=5,.presence_keep_sensitivity=7,.presence_hold_seconds=30};
const panel_settings_t *settings_get(void){return &cfg;}
i2c_master_bus_handle_t board_7c_i2c_bus(void){return no_bus?NULL:(void*)1;}
static void tick(int64_t delta){now+=delta;c4001_poll();}
static void until_ready(void){for(int i=0;i<100 && !c4001_reading().available;i++)tick(200000);assert(c4001_reading().available);}
int main(void){
 tick(2000000);assert(adds==0);
 cfg.presence_enabled=1;no_bus=true;tick(1000000);assert(adds==0);
 no_bus=false;failed=true;tick(1000000);assert(!c4001_reading().available);
 int before=writes;tick(1000000);assert(writes==before); /* retry backoff */
 failed=false;registers[0]=0x83;registers[0x10]=1;until_ready();
 assert(toggles==1 && saves==1 && adds==1 && c4001_reading().occupied);
 assert(registers[0x20]==5 && registers[0x21]==7 && registers[0x22]==0);
 assert(registers[0x23]==60 && registers[0x24]==0 && registers[0x25]==30 && registers[0x26]==0);
 assert(registers[0x27]==0x58 && registers[0x28]==2 && registers[0x29]==0x58 && registers[0x2a]==2);
 registers[0x10]=0;tick(600000);assert(c4001_reading().available && !c4001_reading().occupied);
 failed=true;tick(600000);assert(!c4001_reading().available);
 failed=false;registers[0x10]=1;until_ready();assert(c4001_reading().occupied && saves==1 && adds==1);
 registers[0]=0;tick(600000);assert(!c4001_reading().available); /* sensor reboot */
 registers[0]=0x81;until_ready();assert(saves==1);
 registers[0x20]=0;failed=true;tick(600000);failed=false;corrupt_config=true;
 for(int i=0;i<40;i++)tick(200000);
 assert(!c4001_reading().available); /* rejected readback must not report vacancy */
 corrupt_config=false;until_ready();
 cfg.presence_enabled=0;before=writes;tick(1000000);assert(writes==before);
 /* Other selectable address works on a fresh boot. */
 s_device=NULL;s_stage=CHECK_MODE;s_due_us=0;store_reading(false,false,now);
 cfg.presence_address=expected_address=43;cfg.presence_enabled=1;until_ready();assert(adds==2);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    d=Path(directory)
    (d/'stub.h').write_text(STUB)
    for header in ['esp_err.h','esp_timer.h','driver/i2c_master.h','freertos/FreeRTOS.h']:
        path=d/header;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#include "stub.h"\n')
    (d/'test.c').write_text(TEST)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(d),'-I'+str(ROOT/'main'),str(d/'test.c'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
print('C4001 mode, register encoding, timing, readback, presence, failure and recovery checks passed')
