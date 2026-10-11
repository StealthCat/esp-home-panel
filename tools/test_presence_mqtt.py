#!/usr/bin/env python3
"""Exercise production occupancy MQTT functions with a minimal JSON/MQTT fake."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/mqtt_bridge.c').read_text()
start=source.index('static void publish_presence(void)')
end=source.index('static void publish_home_assistant_discovery(void)',start)
functions=source[start:end]
STUB=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "c4001.h"
static panel_settings_t cfg;
static presence_reading_t reading;
static int64_t now;
static bool s_connected=true;
static const char *s_topic_presence="panel/sensor/occupancy/state";
static const char *s_topic_presence_availability="panel/sensor/occupancy/availability";
static const char *s_discovery_presence_config="homeassistant/binary_sensor/panel_occupancy/config";
static const char *s_topic_availability="panel/availability";
const panel_settings_t *settings_get(void){return &cfg;}
presence_reading_t c4001_reading(void){return reading;}
static int64_t esp_timer_get_time(void){return now;}
struct publication {const char *topic;char payload[32];int retain;};
static struct publication messages[32];static int count;
static int mqtt_publish_logged(const char *topic,const char *payload,int qos,int retain){
 assert(qos==1 && count<32);messages[count].topic=topic;snprintf(messages[count].payload,32,"%s",payload);messages[count++].retain=retain;return 1;
}
typedef struct {const char *key;const char *text;int number;bool numeric;} field;
typedef struct cJSON {field fields[16];int count;struct cJSON *items[4];int children;} cJSON;
static cJSON nodes[8];static int used;static cJSON *availability;
static cJSON *cJSON_CreateObject(void){assert(used<8);return &nodes[used++];}
static void cJSON_AddStringToObject(cJSON *o,const char *k,const char *v){assert(o->count<16);o->fields[o->count++]=(field){k,v,0,false};}
static void cJSON_AddNumberToObject(cJSON *o,const char *k,int v){assert(o->count<16);o->fields[o->count++]=(field){k,NULL,v,true};}
static cJSON *cJSON_AddArrayToObject(cJSON *o,const char *k){(void)o;assert(!strcmp(k,"availability"));availability=cJSON_CreateObject();return availability;}
static void cJSON_AddItemToArray(cJSON *a,cJSON *b){assert(a->children<4);a->items[a->children++]=b;}
static void cJSON_Delete(cJSON *o){(void)o;}
static void add_unique_id(cJSON *o,const panel_settings_t *s,const char *suffix){(void)o;(void)s;assert(!strcmp(suffix,"occupancy"));}
static void add_device_info(cJSON *o,const panel_settings_t *s){(void)o;assert(s==&cfg);}
static field *get(cJSON *o,const char *k){for(int i=0;i<o->count;i++)if(!strcmp(o->fields[i].key,k))return &o->fields[i];assert(0);return NULL;}
static void publish_discovery_json(const char *topic,cJSON *o){
 assert(topic==s_discovery_presence_config);
 assert(!strcmp(get(o,"name")->text,"Occupancy"));
 assert(!strcmp(get(o,"device_class")->text,"occupancy"));
 assert(!strcmp(get(o,"payload_on")->text,"ON") && !strcmp(get(o,"payload_off")->text,"OFF"));
 assert(get(o,"state_topic")->text==s_topic_presence);
 assert(get(o,"expire_after")->number==5);
 assert(!strcmp(get(o,"availability_mode")->text,"all"));
 assert(availability->children==2);
 assert(get(availability->items[0],"topic")->text==s_topic_availability);
 assert(get(availability->items[1],"topic")->text==s_topic_presence_availability);
}
'''
TEST=r'''
int main(void){
 cfg.presence_enabled=1;reading=(presence_reading_t){true,true,1000000};now=1000000;
 publish_presence_discovery(&cfg);
 assert(count==2 && !strcmp(messages[0].payload,"ON") && !messages[0].retain);
 assert(!strcmp(messages[1].payload,"online") && messages[1].retain);
 count=0;reading.occupied=false;publish_presence();assert(!strcmp(messages[0].payload,"OFF"));
 count=0;reading.available=false;publish_presence();assert(count==1 && messages[0].topic==s_topic_presence_availability && !strcmp(messages[0].payload,"offline"));
 count=0;reading.available=true;now=5000000;publish_presence();assert(count==1 && !strcmp(messages[0].payload,"offline"));
 count=0;cfg.presence_enabled=0;publish_presence_discovery(&cfg);
 assert(count==3 && messages[0].topic==s_discovery_presence_config && messages[0].payload[0]==0 && messages[0].retain);
 assert(messages[1].topic==s_topic_presence && messages[1].payload[0]==0 && messages[1].retain);
 count=0;cfg.presence_enabled=1;s_connected=false;publish_presence();assert(count==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    d=Path(directory);(d/'esp_err.h').write_text('typedef int esp_err_t;\n')
    (d/'test.c').write_text(STUB+functions+TEST)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(d),'-I'+str(ROOT/'main'),str(d/'test.c'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
print('Occupancy discovery, states, availability, expiry and discovery removal checks passed')
