#!/usr/bin/env python3
"""Exercise production brightness payloads through the HA package's choose branches."""
import json
from pathlib import Path
import subprocess
import tempfile
import yaml
from jinja2 import Environment
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'main/mqtt_bridge.c').read_text()
functions = []
for name in ('tapo', 'overhead'):
    start = source.index('void mqtt_bridge_' + name + '_brightness(')
    functions.append(source[start:source.index('\n}', start) + 2])
stub = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "settings.h"
#define CONTROL_TAPO 0
#define CONTROL_OVERHEAD 1
static const char *s_topic_cmd_tapo="tapo", *s_topic_cmd_overhead="overhead";
static panel_settings_t cfg;
const panel_settings_t *settings_get(void){return &cfg;}
static void note_local_command(int control){(void)control;}
static unsigned clamp_pct(unsigned value){return value>100?100:value;}
static void publish_targeted_json(const char *topic,const char *entity,const char *payload){
    (void)entity;printf("%s %s\n",topic,payload);
}
'''
test = '''
int main(void){
    unsigned values[]={0,1,25,50,75,100,255};
    for(unsigned i=0;i<7;i++){
        mqtt_bridge_tapo_brightness(values[i]);
        mqtt_bridge_overhead_brightness(values[i]);
    }
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'esp_err.h').write_text('typedef int esp_err_t;\n')
    (tmp / 'test.c').write_text(stub + '\n'.join(functions) + test)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(tmp),
                    '-I'+str(ROOT/'main'), str(tmp/'test.c'), '-o', str(tmp/'test')], check=True)
    output = subprocess.check_output([str(tmp/'test')], text=True)
package = yaml.safe_load((ROOT/'home-assistant/esp_home_panel.yaml').read_text())
automation = next(a for a in package['automation'] if 'actions' in a)
branches = automation['actions'][0]['choose']
env = Environment()
def render(template, payload):
    return env.from_string(template).render(trigger={'payload_json':payload}, entity='light.test').strip()
def selected(control, payload):
    branch = next(b for b in branches if "control == '"+control+"'" in b['conditions'])
    return next(b for b in branch['sequence'][0]['choose']
                if render(b['conditions'], payload) == 'True')['sequence'][0]
for line in output.splitlines():
    control, body = line.split(' ', 1)
    payload = json.loads(body)
    assert payload['on'] == (payload['brightness'] > 0)
    assert 0 <= payload['brightness'] <= 100
    action = selected(control, payload)
    assert action['action'] == 'light.turn_on'
    assert int(render(action['data']['brightness_pct'], payload)) == payload['brightness']
for control in ('tapo', 'overhead'):
    # Legacy firmware and ordinary power buttons must still work.
    assert selected(control, {'brightness':60})['action'] == 'light.turn_on'
    for on in (False, True):
        action = selected(control, {'on':on})
        assert render(action['action'], {'on':on}) == ('light.turn_on' if on else 'light.turn_off')
print('Combined light power/brightness, presets, zero, clamping and legacy HA commands passed')
