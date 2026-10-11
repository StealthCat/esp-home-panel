#!/usr/bin/env python3
"""Production weather parser + MQTT fragmentation, and HA payload templates."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.request
import datetime
import re
import yaml
from jinja2.nativetypes import NativeEnvironment
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/mqtt_bridge.c').read_text()
fragment=source[source.index('static bool receive_weather_fragment('):source.index('static void publish_state_request(')]
TEST=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "weather.c"
#include "settings.h"
static panel_settings_t cfg={.weather_enabled=1};
const panel_settings_t *settings_get(void) {return &cfg;}
int64_t esp_timer_get_time(void) {return 1000000;}
typedef struct {int current_data_offset,total_data_len,data_len;const char *topic,*data;} event_t;
typedef event_t *esp_mqtt_event_handle_t;
static bool topic_equals(esp_mqtt_event_handle_t e,const char *s) {return e->topic && !strcmp(e->topic,s);}
static const char *s_topic_weather="weather",*s_topic_forecast="forecast";
static char *s_weather_rx;
static int s_weather_rx_total,s_weather_rx_used;
static bool s_weather_rx_forecast;
FRAGMENT
int main(void) {
 assert(weather_init());
 const char *station="{\"version\":1,\"available\":true,\"name\":\"Station\",\"age_seconds\":22,\"fields\":[{\"label\":\"Temperature\",\"value\":\"0\",\"unit\":\"°F\"}]}";
 event_t e={.total_data_len=(int)strlen(station),.data_len=10,.topic="weather",.data=station};
 assert(receive_weather_fragment(&e));
 weather_snapshot_t out;weather_copy(&out);assert(out.revision==0);
 e.current_data_offset=10;e.data=station+10;e.data_len=e.total_data_len-10;e.topic=NULL;
 assert(receive_weather_fragment(&e));weather_copy(&out);
 assert(out.field_count==1 && out.station_available && out.station_age_seconds==22);
 assert(!strcmp(out.fields[0].value,"0") && !strcmp(out.fields[0].unit,"°F"));
 unsigned revision=out.revision;
 assert(!weather_receive(false,"{bad",4,1000000));
 assert(!weather_receive(false,"{}",2,1000000));
 const char *missing="{\"version\":1,\"available\":false,\"fields\":[]}";
 assert(weather_receive(false,missing,strlen(missing),2000000));
 weather_copy(&out);assert(!out.station_available && out.field_count==0);
 const char *forecast="{\"version\":1,\"available\":true,\"days\":[{\"date\":\"Mon\",\"condition\":\"rainy\",\"high\":\"75 °F\",\"low\":\"--\",\"rain\":\"90% rain\"}]}";
 assert(weather_receive(true,forecast,strlen(forecast),3000000));
 weather_copy(&out);assert(out.day_count==1 && !strcmp(out.days[0].low,"--"));
 assert(out.revision==revision+2 && !out.station_available);
 // Oversize and out-of-order fragments never mutate a valid snapshot.
 e=(event_t){.total_data_len=WEATHER_MAX_PAYLOAD+1,.data_len=10,.topic="weather",.data=station};
 assert(receive_weather_fragment(&e));assert(!s_weather_rx);
 e.total_data_len=strlen(station);assert(receive_weather_fragment(&e));
 e.current_data_offset=20;e.data=station+20;e.data_len=2;e.topic=NULL;
 assert(receive_weather_fragment(&e));assert(!s_weather_rx);
 weather_copy(&out);assert(out.revision==revision+2);
 // A new non-weather publication cancels partial weather input safely.
 e=(event_t){.total_data_len=(int)strlen(station),.data_len=10,.topic="weather",.data=station};
 assert(receive_weather_fragment(&e));e.topic="other";
 assert(!receive_weather_fragment(&e));assert(!s_weather_rx);
 cJSON *large=cJSON_CreateObject();cJSON_AddNumberToObject(large,"version",1);
 cJSON_AddBoolToObject(large,"available",1);cJSON *fields=cJSON_AddArrayToObject(large,"fields");
 for(int i=0;i<60;i++){cJSON *field=cJSON_CreateObject();cJSON_AddStringToObject(field,"label","Wind");cJSON_AddStringToObject(field,"value","Unavailable");cJSON_AddItemToArray(fields,field);}
 char *payload=cJSON_PrintUnformatted(large);assert(weather_receive(false,payload,strlen(payload),4000000));
 weather_copy(&out);assert(out.field_count==48 && out.truncated && !strcmp(out.fields[0].value,"Unavailable"));
 free(payload);cJSON_Delete(large);free(s_weather);s_weather=NULL;
 return 0;
}
'''.replace('FRAGMENT',fragment)
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory)
    source_dir=Path(os.environ.get('CJSON_DIR',str(Path.home()/'.platformio/packages/framework-espidf/components/json/cJSON')))
    if not (source_dir/'cJSON.c').exists():
        source_dir=tmp
        for name in ('cJSON.c','cJSON.h'):
            with urllib.request.urlopen('https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.19/'+name,timeout=30) as response:
                (tmp/name).write_bytes(response.read())
    (tmp/'test.c').write_text(TEST)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'tests/host/stubs'),'-I'+str(ROOT/'main'),'-I'+str(source_dir),str(tmp/'test.c'),str(source_dir/'cJSON.c'),'-lm','-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
# Render the actual package's payload templates with representative HA objects.
package=yaml.safe_load((ROOT/'home-assistant/esp-home-panel-weather.yaml').read_text())
actions=package['automation'][0]['actions']
station_template=actions[1]['data']['payload']
forecast_template=actions[2]['then'][2]['data']['payload']
env=NativeEnvironment()
env.filters['to_json']=json.dumps
env.tests['match']=lambda value,pattern: re.match(pattern,value) is not None
now=datetime.datetime(2026,10,11,12,tzinfo=datetime.timezone.utc)
class State:
    last_reported=now-datetime.timedelta(seconds=90)
env.globals.update(now=lambda:now,as_timestamp=lambda value,default=0: value.timestamp() if hasattr(value,'timestamp') else default,
    as_datetime=lambda value,default=None: datetime.datetime.fromisoformat(value) if value else default,
    as_local=lambda value:value,device_attr=lambda *args:'Evans Station',expand=lambda entity:[State()],
    has_value=lambda entity:entity=='sensor.outdoor',
    states=lambda entity:'0' if entity=='sensor.outdoor' else 'unavailable',
    state_attr=lambda entity,key:{'friendly_name':entity,'unit_of_measurement':'°F','temperature_unit':'°F','precipitation_unit':'in'}.get(key))
def render(template,**kwargs):
    result=env.from_string(template).render(**kwargs)
    return json.loads(result) if isinstance(result,str) else result
station=render(station_template,station='sensor.outdoor',station_device='station1',station_entities=['sensor.outdoor','sensor.wind'])
assert station['fields'][0]['value']=='0' and station['fields'][1]['value']=='Unavailable'
assert station['fields'][1]['unit']=='' and station['age_seconds']==90
forecast=render(forecast_template,forecast='weather.home',daily={'weather.home':{'forecast':[
    {'datetime':'2026-10-10T12:00:00+00:00','temperature':70},
    {'datetime':'2026-10-11T12:00:00+00:00','condition':'rainy','temperature':75,'templow':None,'precipitation_probability':0}]}})
assert len(forecast['days'])==1 and forecast['days'][0]['low']=='--'
assert forecast['days'][0]['rain']=='0% rain'
assert not render(forecast_template,forecast='weather.home',daily={})['available']
print('Weather schema, missing values, fragmentation, payload limits and HA templates passed')
