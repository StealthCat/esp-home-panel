#!/usr/bin/env python3
"""Compile production settings/web code with fake NVS/HTTP and test color saves."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_ARG -3
#define ESP_ERR_INVALID_SIZE -4
#define ESP_ERR_NVS_NOT_FOUND -5
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(x) ESP_ERROR_CHECK(x)
static const char *esp_err_to_name(int e) {(void)e;return "error";}
static size_t strlcpy(char *d,const char *s,size_t n) {
    size_t len=strlen(s); if(n){size_t k=len<n-1?len:n-1;memcpy(d,s,k);d[k]=0;}return len;
}
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static void *heap_caps_calloc(size_t n,size_t s,int c) {(void)c;return calloc(n,s);}
#define ESP_MAC_WIFI_STA 0
static int esp_read_mac(uint8_t *m,int t) {(void)t;memset(m,1,6);return 0;}
#define CONFIG_PANEL_WIFI_SSID ""
#define CONFIG_PANEL_WIFI_PASSWORD ""
#define CONFIG_PANEL_MQTT_URI ""
#define CONFIG_PANEL_MQTT_USERNAME ""
#define CONFIG_PANEL_MQTT_PASSWORD ""
#define CONFIG_PANEL_MQTT_BASE_TOPIC "home/7c-panel"
#define CONFIG_PANEL_MQTT_DISCOVERY_PREFIX "homeassistant"
#define CONFIG_PANEL_DISPLAY_TITLE "Home Control"
#define CONFIG_PANEL_TIMEZONE "UTC0"
#define CONFIG_PANEL_BACKLIGHT 100
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
static uint32_t stored[6];
static bool present[6];
static int commits;
static uint8_t als_enabled, sensor_interval;
static uint8_t prs_values[17];
static bool prs_present[17];
static const char *prs_keys[]={"prs_enabled","prs_address","prs_max_m","prs_trig_sens","prs_keep_sens","prs_hold_s","sleep_on","sleep_delay","wake_minutes","auto_bright","bright_min","bright_max","weather_on","wx_provider","wx_forecast","wx_units","wx_poll"};
static int prs_index(const char *key){for(int i=0;i<17;i++)if(!strcmp(key,prs_keys[i]))return i;return -1;}
static char saved_hostname[33], saved_admin_password[65], saved_station[128], saved_forecast[129], saved_app[129], saved_fc_key[129];
static int nvs_open(const char *s,int m,nvs_handle_t *h) {(void)s;(void)m;*h=1;return 0;}
static void nvs_close(nvs_handle_t h) {(void)h;}
static int nvs_get_str(int h,const char *k,char *v,size_t *s) {(void)h;if(!strcmp(k,"wx_station")){strlcpy(v,saved_station,*s);return 0;}if(!strcmp(k,"wx_api")){strlcpy(v,saved_forecast,*s);return 0;}if(!strcmp(k,"wx_app")){strlcpy(v,saved_app,*s);return 0;}if(!strcmp(k,"wx_fc_key")){strlcpy(v,saved_fc_key,*s);return 0;}if(!strcmp(k,"hostname") && saved_hostname[0]) {strlcpy(v,saved_hostname,*s);return 0;}if(!strcmp(k,"admin_pass") && saved_admin_password[0]){strlcpy(v,saved_admin_password,*s);return 0;}return ESP_ERR_NVS_NOT_FOUND;}
static int nvs_set_str(int h,const char *k,const char *v) {(void)h;if(!strcmp(k,"wx_station"))strlcpy(saved_station,v,sizeof(saved_station));if(!strcmp(k,"wx_api"))strlcpy(saved_forecast,v,sizeof(saved_forecast));if(!strcmp(k,"wx_app"))strlcpy(saved_app,v,sizeof(saved_app));if(!strcmp(k,"wx_fc_key"))strlcpy(saved_fc_key,v,sizeof(saved_fc_key));if(!strcmp(k,"hostname"))strlcpy(saved_hostname,v,sizeof(saved_hostname));if(!strcmp(k,"admin_pass"))strlcpy(saved_admin_password,v,sizeof(saved_admin_password));return 0;}
static int nvs_get_u8(int h,const char *k,uint8_t *v) {(void)h;int i=prs_index(k);if(i>=0){if(!prs_present[i])return ESP_ERR_NVS_NOT_FOUND;*v=prs_values[i];return 0;}if(!strcmp(k,"als_enabled")){*v=als_enabled;return 0;}if(!strcmp(k,"sensor_interval") && sensor_interval){*v=sensor_interval;return 0;}return ESP_ERR_NVS_NOT_FOUND;}
static int nvs_set_u8(int h,const char *k,uint8_t v) {(void)h;int i=prs_index(k);if(i>=0){prs_present[i]=true;prs_values[i]=v;return 0;}if(!strcmp(k,"als_enabled"))als_enabled=v;if(!strcmp(k,"sensor_interval"))sensor_interval=v;return 0;}
static int color_index(const char *k) {assert(strlen(k)==7 && !strncmp(k,"color_",6));int i=k[6]-'0';assert(i>=0&&i<6);return i;}
static int nvs_get_u32(int h,const char *k,uint32_t *v) {(void)h;int i=color_index(k);if(!present[i])return ESP_ERR_NVS_NOT_FOUND;*v=stored[i];return 0;}
static int nvs_set_u32(int h,const char *k,uint32_t v) {(void)h;int i=color_index(k);stored[i]=v;present[i]=true;return 0;}
static int nvs_commit(int h) {(void)h;commits++;return 0;}
static int nvs_erase_all(int h) {(void)h;saved_station[0]=saved_forecast[0]=saved_app[0]=saved_fc_key[0]=0;memset(present,0,sizeof(present));memset(prs_present,0,sizeof(prs_present));als_enabled=0;sensor_interval=0;saved_hostname[0]=0;saved_admin_password[0]=0;return 0;}
typedef void *httpd_handle_t;
typedef struct {size_t content_len;const char *body;} httpd_req_t;
typedef struct {int max_uri_handlers,stack_size;bool lru_purge_enable;} httpd_config_t;
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t *);} httpd_uri_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HTTPD_RESP_USE_STRLEN -1
#define HTTPD_SOCK_ERR_TIMEOUT -2
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTP_GET 0
#define HTTP_POST 1
static char response[64000];
static int status,reboots;
static int httpd_resp_send_chunk(httpd_req_t *r,const char *s,int n) {(void)r;(void)n;if(s){assert(strlen(response)+strlen(s)<sizeof(response));strcat(response,s);}return 0;}
static int httpd_resp_set_status(httpd_req_t *r,const char *s) {(void)r;status=atoi(s);return 0;}
static int httpd_resp_set_type(httpd_req_t *r,const char *s) {(void)r;(void)s;return 0;}
static int httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v) {(void)r;(void)k;(void)v;return 0;}
static int httpd_resp_send_err(httpd_req_t *r,int e,const char *s) {(void)r;(void)s;status=e;return 0;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *s) {(void)r;(void)s;return 0;}
static int httpd_req_recv(httpd_req_t *r,char *d,size_t n) {memcpy(d,r->body,n);r->body+=n;return (int)n;}
static int httpd_start(httpd_handle_t *h,const httpd_config_t *c) {(void)c;*h=(void*)1;return 0;}
static int httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *u) {(void)h;(void)u;return 0;}
static void esp_restart(void) {reboots++;}
#define pdMS_TO_TICKS(x) (x)
static void vTaskDelay(int t) {(void)t;}
'''

HARNESS = r'''
#define TAG settings_tag
#include "settings.c"
#undef TAG
#include "web_config.c"
void web_security_init(void) {}
bool web_security_authorize(httpd_req_t *r,bool required) {(void)r;(void)required;return true;}
const char *web_security_token(void) {return "test-csrf";}
bool web_security_check_token(const char *t) {return !strcmp(t,"test-csrf");}
esp_err_t ota_update_register(httpd_handle_t server) {(void)server;return ESP_OK;}
bool network_is_connected(void) {return true;}
bool network_config_ap_active(void) {return false;}
void network_identity_get_domain(char *dst, size_t size) {strlcpy(dst,"snoognet.core",size);}
const char *network_get_sta_ip(void) {return "192.0.2.1";}
const char *network_get_config_ap_ssid(void) {return "test";}
const char *network_get_config_ap_password(void) {return "test";}
static void post(const char *body) {
    char form[1024];
    snprintf(form,sizeof(form),"csrf=test-csrf&panel_id=test&display_title=Home&mqtt_discovery_prefix=homeassistant&%s",body);
    httpd_req_t req={.content_len=strlen(form),.body=form};
    status=200;assert(save_handler(&req)==ESP_OK);
}
int main(void) {
    assert(settings_init()==ESP_OK);
    uint32_t defaults[6]={0xFF0000,0xFF8000,0xFFFF00,0x00FF00,0x0066FF,0xA020F0};
    assert(!memcmp(settings_get()->color_presets,defaults,sizeof(defaults)));
    post("color_0=%23123456&color_1=%23ffffff&color_2=%23000000&color_3=%23AbCdEf&color_4=%23010203&color_5=%23e83691");
    uint32_t custom[6]={0x123456,0xFFFFFF,0,0xABCDEF,0x010203,0xE83691};
    assert(status==200 && reboots==1 && commits==1);
    assert(!memcmp(stored,custom,sizeof(custom)));
    assert(settings_init()==ESP_OK); /* Simulate loading after reboot. */
    assert(!memcmp(settings_get()->color_presets,custom,sizeof(custom)));
    httpd_req_t req={0};assert(root_handler(&req)==ESP_OK);
    for(int i=0;i<6;i++) {
        char expected[100];snprintf(expected,sizeof(expected),"id='color_%d' name='color_%d' type='color' value='#%06lx'",i,i,(unsigned long)custom[i]);
        assert(strstr(response,expected));
    }
    const char *bad[]={"", "123456", "%2312345", "%231234567", "%23gg0000", "%23-12345"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        char body[100];snprintf(body,sizeof(body),"color_0=%%23000000&color_5=%s",bad[i]);
        post(body);assert(status==400 && commits==1 && reboots==1);
        assert(!memcmp(settings_get()->color_presets,custom,sizeof(custom)));
    }
    post("theme=2"); /* Older forms must preserve colors. */
    assert(status==200 && !memcmp(stored,custom,sizeof(custom)));
    panel_settings_t invalid=*settings_get();invalid.color_presets[0]=0x1000000;
    assert(settings_save(&invalid)==ESP_ERR_INVALID_ARG);
    stored[0]=0x1000000;assert(settings_init()==ESP_OK);
    assert(settings_get()->color_presets[0]==defaults[0]);
    assert(settings_reset()==ESP_OK);assert(settings_init()==ESP_OK);
    assert(!memcmp(settings_get()->color_presets,defaults,sizeof(defaults)));
    assert(!strcmp(settings_get()->hostname,"panel-010101"));
    post("hostname=bedroom-panel");
    assert(status==200 && !strcmp(settings_get()->hostname,"bedroom-panel"));
    assert(settings_init()==ESP_OK && !strcmp(settings_get()->hostname,"bedroom-panel"));
    int saved_commits=commits, saved_reboots=reboots;
    const char *bad_hosts[]={"", "-panel", "panel-", "panel.local", "panel_name", "Panel", "a%20b", "a%00b",
        "abcdefghijklmnopqrstuvwxyz1234567", "%3Cscript%3E"};
    for(size_t i=0;i<sizeof(bad_hosts)/sizeof(bad_hosts[0]);i++) {
        char body[200];snprintf(body,sizeof(body),"hostname=%s",bad_hosts[i]);
        post(body);assert(status==400 && commits==saved_commits && reboots==saved_reboots);
    }
    post("theme=1");assert(status==200 && !strcmp(settings_get()->hostname,"bedroom-panel"));
    post("hostname=abcdefghijklmnopqrstuvwxyz123456");assert(status==200);
    response[0]=0;assert(root_handler(&req)==ESP_OK);
    assert(strstr(response,"abcdefghijklmnopqrstuvwxyz123456.snoognet.core"));
    assert(strstr(response,"abcdefghijklmnopqrstuvwxyz123456.local"));
    invalid=*settings_get();memset(invalid.hostname,'x',sizeof(invalid.hostname));
    assert(settings_save(&invalid)==ESP_ERR_INVALID_ARG);
    assert(settings_reset()==ESP_OK);assert(settings_init()==ESP_OK);
    assert(!strcmp(settings_get()->hostname,"panel-010101"));
    post("admin_password=short");assert(status==400);
    post("admin_password=good-password");assert(status==200);
    assert(!strcmp(settings_get()->admin_password,"good-password"));
    assert(settings_init()==ESP_OK && !strcmp(settings_get()->admin_password,"good-password"));
    post("theme=2");assert(status==200 && !strcmp(settings_get()->admin_password,"good-password"));
    httpd_req_t no_csrf={.content_len=7,.body="theme=1"};
    status=200;assert(save_handler(&no_csrf)==ESP_OK && status==403);
    assert(settings_get()->sensor_interval_seconds==10 && !settings_get()->ambient_light_enabled);
    post("ambient_light_enabled=1&sensor_interval_seconds=5");assert(status==200);
    assert(settings_init()==ESP_OK && settings_get()->ambient_light_enabled==1 && settings_get()->sensor_interval_seconds==5);
    post("sensor_interval_seconds=0");assert(status==400);
    post("sensor_interval_seconds=61");assert(status==400);
    post("sensor_interval_seconds=3x");assert(status==400);
    post("ambient_light_enabled=2");assert(status==400);
    post("theme=1");assert(status==200 && settings_get()->ambient_light_enabled==1);
    post("ambient_light_enabled=0");assert(status==200 && !settings_get()->ambient_light_enabled);
    assert(settings_reset()==ESP_OK && settings_init()==ESP_OK);
    assert(settings_get()->sensor_interval_seconds==10 && !settings_get()->ambient_light_enabled);
    assert(!settings_get()->presence_enabled && settings_get()->presence_address==42 && settings_get()->presence_hold_seconds==30);
    post("presence_enabled=1&presence_address=43&presence_max_m=8&presence_trigger_sensitivity=9&presence_keep_sensitivity=0&presence_hold_seconds=120");
    assert(status==200 && settings_init()==ESP_OK);
    const panel_settings_t *pc=settings_get();
    assert(pc->presence_enabled==1 && pc->presence_address==43 && pc->presence_max_m==8 && pc->presence_trigger_sensitivity==9 && pc->presence_keep_sensitivity==0 && pc->presence_hold_seconds==120);
    const char *bad_presence[]={"presence_enabled=2","presence_address=44","presence_address=41","presence_max_m=9","presence_max_m=2","presence_trigger_sensitivity=10","presence_keep_sensitivity=-1","presence_hold_seconds=1","presence_hold_seconds=121","presence_hold_seconds=3x"};
    for(unsigned i=0;i<sizeof(bad_presence)/sizeof(bad_presence[0]);i++){int before=commits;post(bad_presence[i]);assert(status==400 && commits==before);}
    post("theme=0");assert(status==200 && settings_get()->presence_address==43);
    invalid=*settings_get();invalid.presence_address=0;assert(settings_save(&invalid)==ESP_ERR_INVALID_ARG);
    prs_values[1]=255;assert(settings_init()==ESP_OK && settings_get()->presence_address==42);
    assert(settings_reset()==ESP_OK && settings_init()==ESP_OK && !settings_get()->presence_enabled);
    assert(!settings_get()->sleep_enabled && !settings_get()->weather_enabled && !settings_get()->auto_brightness);
    post("sleep_enabled=1&sleep_delay_seconds=90&touch_wake_minutes=5&auto_brightness=1&brightness_min=15&brightness_max=85&weather_enabled=1&weather_station=KTEST1&weather_api_key=secret-key");
    assert(status==200 && settings_init()==ESP_OK);
    assert(settings_get()->sleep_enabled && settings_get()->sleep_delay_seconds==90 && settings_get()->touch_wake_minutes==5);
    assert(settings_get()->auto_brightness && settings_get()->brightness_min==15 && settings_get()->brightness_max==85);
    assert(settings_get()->weather_enabled && !strcmp(settings_get()->weather_station,"KTEST1") && !strcmp(settings_get()->weather_api_key,"secret-key"));
    post("weather_api_key=&weather_app_key=app-secret&weather_forecast_key=forecast-secret");
    assert(status==200 && !strcmp(settings_get()->weather_api_key,"secret-key"));
    assert(settings_init()==ESP_OK && !strcmp(settings_get()->weather_app_key,"app-secret") && !strcmp(settings_get()->weather_forecast_key,"forecast-secret"));
    post("weather_provider=1&weather_forecast=1");assert(status==400);
    post("weather_provider=1&weather_forecast=1&weather_api_key=ambient-secret");assert(status==200);
    post("weather_provider=0&weather_api_key=secret-key&weather_forecast=0&clear_weather_app_key=1&clear_weather_forecast_key=1");assert(status==200);
    assert(!settings_get()->weather_app_key[0] && !settings_get()->weather_forecast_key[0]);
    const char *bad_weather[]={"sleep_enabled=2","sleep_delay_seconds=0","touch_wake_minutes=31","brightness_min=90&brightness_max=80","weather_provider=2","weather_forecast=3","weather_poll_minutes=0","weather_station=bad%0Aname","weather_provider=1"};
    for(unsigned i=0;i<sizeof(bad_weather)/sizeof(bad_weather[0]);i++){int before=commits;post(bad_weather[i]);assert(status==400 && commits==before);}
    post("theme=1");assert(status==200 && settings_get()->weather_enabled && settings_get()->sleep_enabled);
    assert(settings_reset()==ESP_OK && settings_init()==ESP_OK && !settings_get()->weather_enabled && !settings_get()->sleep_enabled);
    puts("Display/weather defaults, persistence, legacy forms, validation and reset passed");
    puts("C4001 settings persistence, legacy forms, invalid NVS and validation checks passed");
    puts("Sensor settings persistence, validation and reset checks passed");
    puts("Hostname save, reload, migration, validation, display and reset checks passed");
    puts("Color form, validation, NVS reload, migration and reset checks passed");
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix="panel-colors-") as directory:
        tmp = Path(directory)
        (tmp / "stubs.h").write_text(STUBS)
        for name in ("esp_err.h", "esp_heap_caps.h", "esp_log.h", "esp_mac.h",
                     "nvs.h", "sdkconfig.h", "esp_http_server.h", "esp_system.h",
                     "freertos/FreeRTOS.h", "freertos/task.h"):
            path = tmp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "stubs.h"\n')
        (tmp / "test.c").write_text(HARNESS)
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                        "-Werror=implicit-function-declaration", "-Wno-unused-function",
                        "-Wno-unused-variable", "-I", str(tmp), "-I", str(ROOT / "main"),
                        str(tmp / "test.c"), "-o", str(tmp / "test")], check=True)
        subprocess.run([str(tmp / "test")], check=True)

if __name__ == "__main__":
    main()

