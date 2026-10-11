#!/usr/bin/env python3
"""Host fault-injection tests for production OTA, boot checks and web authorization."""
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
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_ERROR_CHECK(x) assert((x)==0)
static const char *esp_err_to_name(int n){(void)n;return "error";}
#define strlcpy test_strlcpy
static size_t strlcpy(char *d,const char *s,size_t n){size_t l=strlen(s);if(n){size_t k=l<n-1?l:n-1;memcpy(d,s,k);d[k]=0;}return l;}
typedef void *httpd_handle_t;
typedef struct {size_t content_len;const unsigned char *body;size_t position;void *handle;const char *auth,*token,*type;} httpd_req_t;
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t*);} httpd_uri_t;
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_SOCK_ERR_TIMEOUT -2
static int status,closed,reboots,registered;
static char response[1024];
static int read_chunk,read_failure,read_calls;
static int httpd_resp_set_status(httpd_req_t *r,const char *s){(void)r;status=atoi(s);return 0;}
static int httpd_resp_set_hdr(httpd_req_t *r,const char *a,const char *b){(void)r;(void)a;(void)b;return 0;}
static int httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return 0;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *s){(void)r;strlcpy(response,s,sizeof(response));return 0;}
static int httpd_resp_send(httpd_req_t *r,const char *s,size_t n){(void)r;(void)s;(void)n;return 0;}
static int httpd_req_to_sockfd(httpd_req_t *r){(void)r;return 1;}
static int httpd_sess_trigger_close(void *h,int s){(void)h;(void)s;closed++;return 0;}
static int httpd_register_uri_handler(void *h,const httpd_uri_t *u){(void)h;(void)u;registered++;return 0;}
static int httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *dst,size_t n){
 const char *s=!strcmp(key,"Authorization")?r->auth:!strcmp(key,"X-OTA-Token")?r->token:r->type;
 if(!s||strlen(s)>=n)return -1;
 strlcpy(dst,s,n);return 0;
}
static int httpd_req_recv(httpd_req_t *r,char *dst,size_t wanted){
 read_calls++;if(read_failure && read_calls>2)return read_failure;
 size_t n=wanted;if(read_chunk>0 && n>(size_t)read_chunk)n=read_chunk;
 memcpy(dst,r->body+r->position,n);r->position+=n;return n;
}
static void esp_fill_random(void *d,size_t n){memset(d,0x5a,n);}
static int mbedtls_base64_encode(unsigned char *dst,size_t cap,size_t *olen,const unsigned char *src,size_t n){
 const char *a="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t j=0;
 if(cap<4*((n+2)/3))return -1;
 for(size_t i=0;i<n;i+=3){unsigned v=(unsigned)src[i]<<16;if(i+1<n)v|=(unsigned)src[i+1]<<8;if(i+2<n)v|=src[i+2];dst[j++]=a[v>>18];dst[j++]=a[(v>>12)&63];dst[j++]=i+1<n?a[(v>>6)&63]:'=';dst[j++]=i+2<n?a[v&63]:'=';}*olen=j;return 0;
}
typedef struct {unsigned address,size;char label[16];} esp_partition_t;
typedef int esp_ota_img_states_t;
#define ESP_OTA_IMG_PENDING_VERIFY 1
#define ESP_OTA_IMG_VALID 2
#define ESP_IMAGE_HEADER_MAGIC 0xe9
#define ESP_APP_DESC_MAGIC_WORD 0xabcd5432
#define ESP_CHIP_ID_ESP32S3 9
typedef struct {uint8_t magic,segment_count;uint16_t chip_id;uint8_t pad[20];} esp_image_header_t;
typedef struct {uint32_t load_addr,data_len;} esp_image_segment_header_t;
typedef struct {uint32_t magic_word;char version[32],project_name[32];uint8_t app_elf_sha256[32];} esp_app_desc_t;
typedef int esp_ota_handle_t;
static esp_partition_t running={0x20000,0x700000,"ota_0"},target={0x720000,0x700000,"ota_1"};
static esp_app_desc_t current={.magic_word=ESP_APP_DESC_MAGIC_WORD,.version="test",.project_name="esp_home_panel_7c"};
static int ota_state=ESP_OTA_IMG_VALID,begin_count,write_count,abort_count,end_count,boot_count,confirm_count;
static int fail_begin,fail_write,fail_end,fail_boot,fail_confirm;
static size_t written;
static const esp_app_desc_t *esp_app_get_description(void){return &current;}
static void esp_app_get_elf_sha256(char *s,size_t n){strlcpy(s,"1234",n);}
static const esp_partition_t *esp_ota_get_running_partition(void){return &running;}
static const esp_partition_t *esp_ota_get_next_update_partition(void *p){(void)p;return &target;}
static int esp_ota_get_state_partition(const esp_partition_t *p,int *s){(void)p;*s=ota_state;return 0;}
static int esp_ota_begin(const esp_partition_t *p,size_t n,int *h){assert(p==&target);assert(n<=p->size);begin_count++;*h=1;return fail_begin;}
static int esp_ota_write(int h,const void *data,size_t n){(void)data;assert(h==1);write_count++;written+=n;return fail_write;}
static int esp_ota_abort(int h){assert(h==1);abort_count++;return 0;}
static int esp_ota_end(int h){assert(h==1);end_count++;return fail_end;}
static int esp_ota_set_boot_partition(const esp_partition_t *p){assert(p==&target);boot_count++;return fail_boot;}
static int esp_ota_mark_app_valid_cancel_rollback(void){confirm_count++;if(!fail_confirm)ota_state=ESP_OTA_IMG_VALID;return fail_confirm;}
static void esp_restart(void){reboots++;}
#define pdMS_TO_TICKS(x) (x)
static void vTaskDelay(int n){(void)n;}
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void*);const char *name;} esp_timer_create_args_t;
static int64_t now;static int timer_started,timer_stopped;
static int64_t esp_timer_get_time(void){return now;}
static int esp_timer_create(const esp_timer_create_args_t *a,void **h){assert(a->callback);*h=(void*)1;return 0;}
static int esp_timer_start_once(void *h,int64_t t){(void)h;assert(t==90000000);timer_started++;return 0;}
static int esp_timer_stop(void *h){(void)h;timer_stopped++;return 0;}
static int esp_timer_delete(void *h){(void)h;return 0;}
typedef struct {int dummy;} cJSON;
static cJSON *cJSON_CreateObject(void){return calloc(1,sizeof(cJSON));}
static void cJSON_AddStringToObject(cJSON *j,const char *k,const char *v){(void)j;(void)k;(void)v;}
static void cJSON_AddNumberToObject(cJSON *j,const char *k,double v){(void)j;(void)k;(void)v;}
static void cJSON_AddBoolToObject(cJSON *j,const char *k,bool v){(void)j;(void)k;(void)v;}
static char *cJSON_PrintUnformatted(cJSON *j){(void)j;char *b=malloc(3);memcpy(b,"{}",3);return b;}
static void cJSON_Delete(cJSON *j){free(j);}
'''
HARNESS = r'''
#include "settings.h"
static panel_settings_t settings;
const panel_settings_t *settings_get(void){return &settings;}
#include "web_security.c"
#include "ota_update.c"
const unsigned char firmware_html_start[] asm("_binary_firmware_html_start")="page";
const unsigned char firmware_html_end[] asm("_binary_firmware_html_end")="";
static unsigned char image[9000];
static httpd_req_t request;
static void reset(void){
 memset(image,0,sizeof(image));esp_image_header_t h={.magic=ESP_IMAGE_HEADER_MAGIC,.segment_count=4,.chip_id=ESP_CHIP_ID_ESP32S3};
 memcpy(image,&h,sizeof(h));memcpy(image+sizeof(h)+sizeof(esp_image_segment_header_t),&current,sizeof(current));
 request=(httpd_req_t){.content_len=sizeof(image),.body=image,.auth="Basic YWRtaW46cGFzc3dvcmQx",.token=web_security_token(),.type="application/octet-stream"};
 strlcpy(settings.admin_password,"password1",sizeof(settings.admin_password));
 status=200;closed=reboots=read_calls=begin_count=write_count=abort_count=end_count=boot_count=0;
 fail_begin=fail_write=fail_end=fail_boot=read_failure=0;written=0;read_chunk=7;ota_state=ESP_OTA_IMG_VALID;now=0;
 target.address=0x720000;
}
static void upload(void){assert(firmware_upload(&request)==ESP_OK);}
int main(void){
 web_security_init();assert(strlen(web_security_token())==32);assert(!web_security_check_token("wrong"));
 reset();upload();assert(status==200 && begin_count==1 && end_count==1 && boot_count==1 && reboots==1 && abort_count==0 && written==sizeof(image));
 reset();settings.admin_password[0]=0;assert(web_security_authorize(&request,false));
 reset();request.auth="Basic wrong";upload();assert(status==401 && !begin_count && !read_calls);
 reset();settings.admin_password[0]=0;upload();assert(status==403 && !begin_count);
 reset();request.token="stale";upload();assert(status==403 && !begin_count);
 reset();request.type="multipart/form-data";upload();assert(status==415 && !begin_count);
 reset();request.content_len=target.size+1;upload();assert(status==400 && !begin_count);
 reset();request.content_len=1;upload();assert(status==400 && !begin_count);
 reset();image[0]=0;upload();assert(status==400 && !begin_count);
 reset();esp_image_header_t h;memcpy(&h,image,sizeof(h));h.chip_id=0;memcpy(image,&h,sizeof(h));upload();assert(status==400 && !begin_count);
 reset();image[sizeof(h)+sizeof(esp_image_segment_header_t)]=0;upload();assert(status==400 && !begin_count);
 reset();esp_app_desc_t other=current;strcpy(other.project_name,"wrong_project");memcpy(image+sizeof(h)+sizeof(esp_image_segment_header_t),&other,sizeof(other));upload();assert(status==400 && !begin_count);
 reset();target.address=running.address;upload();assert(status==409 && !begin_count);
 reset();ota_state=ESP_OTA_IMG_PENDING_VERIFY;upload();assert(status==409 && !begin_count);
 reset();fail_begin=-1;upload();assert(status==500 && !abort_count && !boot_count);
 reset();fail_write=-1;upload();assert(status==500 && abort_count==1 && !end_count && !boot_count);
 reset();fail_end=-1;upload();assert(status==400 && !abort_count && !boot_count);
 reset();fail_boot=-1;upload();assert(status==500 && !reboots && !abort_count);
 reset();read_chunk=4096;read_failure=-1;upload();assert(status==400 && abort_count==1 && !boot_count);
 reset();read_failure=HTTPD_SOCK_ERR_TIMEOUT;upload();assert(status==400 && !begin_count && read_calls==5);
 reset();read_chunk=4096;read_failure=HTTPD_SOCK_ERR_TIMEOUT;upload();assert(status==400 && abort_count==1 && !boot_count);
 reset();assert(ota_update_register(NULL)==0 && registered==3);
 ota_state=ESP_OTA_IMG_PENDING_VERIFY;ota_update_boot_begin();assert(timer_started==1);
 now=29000000;ota_update_boot_poll(true);assert(confirm_count==0);
 now=31000000;ota_update_boot_poll(false);assert(confirm_count==0);
 fail_confirm=-1;ota_update_boot_poll(true);assert(s_pending_verify && !timer_stopped);
 fail_confirm=0;ota_update_boot_poll(true);assert(!s_pending_verify && timer_stopped==1 && ota_state==ESP_OTA_IMG_VALID);
 int confirms=confirm_count;ota_update_boot_poll(true);assert(confirm_count==confirms);
 boot_timeout(NULL);assert(reboots==1);
 puts("OTA authentication, CSRF, header/size checks, fragmented reads, failure cleanup, boot selection and rollback checks passed");
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='panel-ota-') as directory:
        tmp=Path(directory)
        (tmp/'stubs.h').write_text(STUBS)
        for name in ('esp_err.h','esp_http_server.h','esp_random.h','mbedtls/base64.h',
                     'cJSON.h','esp_app_desc.h','esp_app_format.h','esp_log.h','esp_ota_ops.h',
                     'esp_system.h','esp_timer.h','freertos/FreeRTOS.h','freertos/task.h'):
            path=tmp/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#include "stubs.h"\n')
        (tmp/'test.c').write_text(HARNESS)
        subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra',
                        '-Werror=implicit-function-declaration','-Wno-unused-function','-Wno-unused-variable',
                        '-fsanitize=address,undefined','-I',str(tmp),'-I',str(ROOT/'main'),
                        str(tmp/'test.c'),'-o',str(tmp/'test')],check=True)
        subprocess.run([str(tmp/'test')],check=True)

if __name__=='__main__': main()
