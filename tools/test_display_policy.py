#!/usr/bin/env python3
"""Exercise the real display policy without Wi-Fi, hardware or wall-clock time."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
TEST = r'''
#include <assert.h>
#include <math.h>
#include "display_policy.h"
int main(void) {
 panel_settings_t cfg={.sleep_enabled=1,.presence_enabled=1,.sleep_delay_seconds=60,
   .touch_wake_minutes=2,.backlight_percent=70,.brightness_min=10,.brightness_max=80,
   .auto_brightness=1,.ambient_light_enabled=1};
 display_policy_t s={.brightness=70};
 assert(display_policy_step(&s,&cfg,1000000,true,false,false,0,false)==100);
 assert(display_policy_step(&s,&cfg,60999999,true,false,false,0,false)==100);
 assert(display_policy_step(&s,&cfg,61000000,true,false,false,0,false)==0);
 assert(s.asleep);
 display_policy_touch(&s,&cfg,62000000);
 assert(display_policy_step(&s,&cfg,181999999,true,false,false,0,false)==100);
 assert(display_policy_step(&s,&cfg,182000000,true,false,false,0,false)==0);
 // Occupancy and radar failure both restore the display; no MQTT involved.
 assert(display_policy_step(&s,&cfg,183000000,true,true,false,0,false)==100);
 assert(display_policy_step(&s,&cfg,184000000,false,false,false,0,false)==100);
 assert(display_policy_step(&s,&cfg,185000000,true,false,false,0,false)==100);
 cfg.sleep_enabled=0;
 for(int i=0;i<100;i++) display_policy_step(&s,&cfg,200000000,false,false,true,0,true);
 assert(s.brightness==10);
 for(int i=0;i<100;i++) display_policy_step(&s,&cfg,200000000,false,false,true,1000,true);
 assert(s.brightness==80);
 for(int i=0;i<100;i++) display_policy_step(&s,&cfg,200000000,false,false,false,0,true);
 assert(s.brightness==70); // failure fallback
 display_policy_step(&s,&cfg,200000000,false,false,true,NAN,true);assert(!s.have_lux);
 cfg.auto_brightness=0;cfg.backlight_percent=30;
 for(int i=0;i<100;i++) display_policy_step(&s,&cfg,200000000,false,false,true,1000,true);
 assert(s.brightness==30);
 cfg.backlight_percent=0;display_policy_touch(&s,&cfg,300000000);
 assert(display_policy_step(&s,&cfg,300000000,false,false,false,0,false)==100);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory)
    (tmp/'esp_err.h').write_text('typedef int esp_err_t;\n')
    (tmp/'test.c').write_text(TEST)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(tmp),'-I'+str(ROOT/'main'),str(tmp/'test.c'),str(ROOT/'main/display_policy.c'),'-lm','-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('Display sleep, touch leases, sensor failure, PWM limits and brightness fallback passed')
