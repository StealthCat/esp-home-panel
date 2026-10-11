#!/usr/bin/env python3
"""Exercise production direct-provider parsers and request safety helpers."""
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.request
ROOT=Path(__file__).resolve().parents[1]
TEST=r'''
#include <assert.h>
#include <math.h>
#include <string.h>
#include "weather.h"
#define STATION(s,a,id) weather_parse_station(&w,s,strlen(s),a,id,false)
#define FORECAST(s,wu) weather_parse_forecast(&w,s,strlen(s),wu,1780000000)
int main(void) {
 weather_snapshot_t w;weather_reset(&w);assert(isnan(w.current.temp));
 const char *wu="{\"observations\":[{\"stationID\":\"KTEST1\",\"epoch\":1780000000,\"lat\":40,\"lon\":-75,\"humidity\":0,\"winddir\":0,\"metric\":{\"temp\":0,\"windSpeed\":0,\"precipTotal\":0,\"pressure\":1013}}]}";
 assert(STATION(wu,false,"ktest1"));assert(w.current.temp==0 && w.current.humidity==0 && w.current.rain_day==0);assert(isnan(w.current.gust));assert(w.latitude==40);
 assert(!STATION(wu,false,"OTHER"));assert(!STATION("{bad",false,"KTEST1"));
 const char *aw="[{\"macAddress\":\"AA:BB:CC:DD:EE:FF\",\"info\":{\"name\":\"Garden\",\"coords\":{\"geo\":{\"coordinates\":[-75,40]}}},\"lastData\":{\"dateutc\":1780000000000,\"tempf\":77,\"humidity\":60,\"windspeedmph\":10,\"baromrelin\":30,\"dailyrainin\":1,\"temp1f\":32}}]";
 assert(STATION(aw,true,"Garden"));assert(fabs(w.current.temp-25)<0.01 && fabs(w.current.wind-16.09344)<0.01 && fabs(w.current.rain_day-25.4)<0.01);assert(w.longitude==-75 && isfinite(w.current.dew));
 assert(STATION(aw,true,"aabbccddeeff"));assert(!STATION(aw,true,"wrong"));
 const char *ambiguous="[{\"info\":{\"name\":\"X\"},\"lastData\":{}},{\"info\":{\"name\":\"X\"},\"lastData\":{}}]";assert(!STATION(ambiguous,true,"X"));
 const char *fc="{\"daily\":{\"time\":[1779800000,1780000000,1780086400],\"temperature_2m_max\":[20,25,26],\"temperature_2m_min\":[10,null,12],\"weather_code\":[0,95,61],\"precipitation_probability_max\":[0,0,70]},\"hourly\":{\"time\":[1779900000,1780000000,1780003600],\"temperature_2m\":[10,20,null],\"precipitation_probability\":[0,0,50]}}";
 assert(FORECAST(fc,false));assert(w.day_count==2 && w.days[0].icon==WX_STORM && isnan(w.days[0].low) && w.days[0].rain_chance==0);assert(w.hour_count==2 && isnan(w.hours[1].temp));
 const char *twc="{\"validTimeUtc\":[1780000000],\"temperatureMax\":[null],\"temperatureMin\":[12],\"narrative\":[\"Evening rain\"],\"daypart\":[{\"iconCode\":[null,12],\"precipChance\":[null,80]}]}";
 assert(FORECAST(twc,true));assert(w.day_count==1 && w.days[0].icon==WX_RAIN && w.days[0].rain_chance==80 && !strcmp(w.days[0].narrative,"Evening rain"));assert(!FORECAST("{}",true));
 char b[80];weather_format(b,sizeof(b),0,"temp",false);assert(!strcmp(b,"32.0 °F"));weather_format(b,sizeof(b),NAN,"wind",true);assert(!strcmp(b,"--"));
 assert(weather_encode(b,sizeof(b),"A B&x=/"));assert(!strcmp(b,"A%20B%26x%3D%2F"));assert(!weather_encode(b,3,"&"));
 assert(weather_retry_seconds(0,500,0)==60 && weather_retry_seconds(20,500,0)==900 && weather_retry_seconds(0,403,0)==900 && weather_retry_seconds(0,429,7200)==3600);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory)
    source_dir=Path(os.environ.get('CJSON_DIR',str(Path.home()/'.platformio/packages/framework-espidf/components/json/cJSON')))
    if not (source_dir/'cJSON.c').exists():
        source_dir=tmp
        for name in ('cJSON.c','cJSON.h'):
            with urllib.request.urlopen('https://raw.githubusercontent.com/DaveGamble/cJSON/v1.7.19/'+name,timeout=30) as response:
                (tmp/name).write_bytes(response.read())
    (tmp/'test.c').write_text(TEST)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'tests/host/stubs'),'-I'+str(ROOT/'main'),'-I'+str(source_dir),str(tmp/'test.c'),str(ROOT/'main/weather_parse.c'),str(source_dir/'cJSON.c'),'-lm','-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('Direct weather provider parsing, units, missing values, station selection, forecasts and retry checks passed')
# Exercise the actual transport collector with fragmented, oversized and failed responses.
worker=(ROOT/'main/weather.c').read_text()
transport=worker[worker.index('typedef struct {char *body;'):worker.index('static void error_text(')]
HTTP=r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "weather.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define HTTP_EVENT_ON_HEADER 1
#define HTTP_EVENT_ON_DATA 2
typedef struct {void *user_data;int event_id;char *header_key,*header_value;int data_len;void *data;} esp_http_client_event_t;
static int64_t clock_us;
static int64_t esp_timer_get_time(void){return clock_us;}
static int esp_crt_bundle_attach(void *p){(void)p;return 0;}
typedef struct {const char *url;esp_err_t (*event_handler)(esp_http_client_event_t *);void *user_data;int (*crt_bundle_attach)(void *);int timeout_ms;bool disable_auto_redirect;int buffer_size,buffer_size_tx;const char *user_agent;bool keep_alive_enable;} esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
static esp_http_client_config_t conf;
static int mode,status_code=200;
static bool complete=true;
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){conf=*c;assert(c->crt_bundle_attach==esp_crt_bundle_attach && c->disable_auto_redirect && c->timeout_ms<=12000);return &conf;}
static int esp_http_client_set_header(void *c,const char *k,const char *v){(void)c;(void)k;(void)v;return 0;}
static int esp_http_client_perform(void *c){
 (void)c;esp_http_client_event_t e={.user_data=conf.user_data,.event_id=HTTP_EVENT_ON_HEADER,.header_key="Retry-After",.header_value="120"};conf.event_handler(&e);
 e.event_id=HTTP_EVENT_ON_DATA;e.data="{}";e.data_len=1;conf.event_handler(&e);
 e.data="}";e.data_len=mode==1?WEATHER_MAX_PAYLOAD:1;
 if(mode==2)clock_us+=30000000;
 conf.event_handler(&e);return mode==3?ESP_FAIL:ESP_OK;
}
static int esp_http_client_get_status_code(void *c){(void)c;return status_code;}
static bool esp_http_client_is_complete_data_received(void *c){(void)c;return complete;}
static void esp_http_client_cleanup(void *c){(void)c;}
TRANSPORT
int main(void){
 response_t r={.body=malloc(WEATHER_MAX_PAYLOAD+1)};int status;
 assert(get_json("https://example.test",&r,&status) && !strcmp(r.body,"{}") && r.retry_after==120);
 for(mode=1;mode<=3;mode++)assert(!get_json("https://example.test",&r,&status));
 mode=0;complete=false;assert(!get_json("https://example.test",&r,&status));complete=true;
 status_code=403;assert(!get_json("https://example.test",&r,&status) && status==403);
 free(r.body);return 0;
}
'''.replace('TRANSPORT',transport)
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);(tmp/'http.c').write_text(HTTP)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'main'),str(tmp/'http.c'),'-o',str(tmp/'http')],check=True)
    subprocess.run([str(tmp/'http')],check=True)
print('HTTPS certificate/redirect configuration, chunk limits, timeout rejection and response failures passed')
