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
static int nvs_open(const char *s,int m,nvs_handle_t *h) {(void)s;(void)m;*h=1;return 0;}
static void nvs_close(nvs_handle_t h) {(void)h;}
static int nvs_get_str(int h,const char *k,char *v,size_t *s) {(void)h;(void)k;(void)v;(void)s;return ESP_ERR_NVS_NOT_FOUND;}
static int nvs_set_str(int h,const char *k,const char *v) {(void)h;(void)k;(void)v;return 0;}
static int nvs_get_u8(int h,const char *k,uint8_t *v) {(void)h;(void)k;(void)v;return ESP_ERR_NVS_NOT_FOUND;}
static int nvs_set_u8(int h,const char *k,uint8_t v) {(void)h;(void)k;(void)v;return 0;}
static int color_index(const char *k) {assert(strlen(k)==7 && !strncmp(k,"color_",6));int i=k[6]-'0';assert(i>=0&&i<6);return i;}
static int nvs_get_u32(int h,const char *k,uint32_t *v) {(void)h;int i=color_index(k);if(!present[i])return ESP_ERR_NVS_NOT_FOUND;*v=stored[i];return 0;}
static int nvs_set_u32(int h,const char *k,uint32_t v) {(void)h;int i=color_index(k);stored[i]=v;present[i]=true;return 0;}
static int nvs_commit(int h) {(void)h;commits++;return 0;}
static int nvs_erase_all(int h) {(void)h;memset(present,0,sizeof(present));return 0;}
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
static char response[24000];
static int status,reboots;
static int httpd_resp_send_chunk(httpd_req_t *r,const char *s,int n) {(void)r;(void)n;if(s)strcat(response,s);return 0;}
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
bool network_is_connected(void) {return true;}
bool network_config_ap_active(void) {return false;}
const char *network_get_sta_ip(void) {return "192.0.2.1";}
const char *network_get_config_ap_ssid(void) {return "test";}
const char *network_get_config_ap_password(void) {return "test";}
static void post(const char *body) {
    char form[1024];
    snprintf(form,sizeof(form),"panel_id=test&display_title=Home&mqtt_discovery_prefix=homeassistant&%s",body);
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
