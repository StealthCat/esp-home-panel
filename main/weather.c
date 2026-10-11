#include "weather.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "settings.h"
#include "network.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static weather_snapshot_t *s_weather;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static bool s_started;
bool weather_init(void)
{
    if(s_weather)return true;
    s_weather=heap_caps_malloc(sizeof(*s_weather),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s_weather)return false;
    weather_reset(s_weather);
    snprintf(s_weather->station_status,sizeof(s_weather->station_status),"Waiting for Wi-Fi and clock sync");
    return true;
}
bool weather_copy(weather_snapshot_t *out)
{
    if(!out||!s_weather)return false;
    portENTER_CRITICAL(&s_lock);memcpy(out,s_weather,sizeof(*out));portEXIT_CRITICAL(&s_lock);return true;
}
static void publish(weather_snapshot_t *next)
{
    portENTER_CRITICAL(&s_lock);
    next->revision=s_weather->revision+1;memcpy(s_weather,next,sizeof(*next));
    portEXIT_CRITICAL(&s_lock);
}
typedef struct {char *body;size_t used;int64_t deadline;unsigned retry_after;bool invalid;} response_t;
static esp_err_t http_event(esp_http_client_event_t *event)
{
    response_t *r=event->user_data;
    if(esp_timer_get_time()>r->deadline){r->invalid=true;return ESP_FAIL;}
    if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key && event->header_value && !strcasecmp(event->header_key,"Retry-After")){
        char *end;unsigned long seconds=strtoul(event->header_value,&end,10);
        if(end!=event->header_value && !*end)r->retry_after=seconds>3600?3600:(unsigned)seconds;
    }
    if(event->event_id==HTTP_EVENT_ON_DATA){
        if(event->data_len<0 || (size_t)event->data_len>WEATHER_MAX_PAYLOAD-r->used){r->invalid=true;return ESP_FAIL;}
        memcpy(r->body+r->used,event->data,event->data_len);r->used+=event->data_len;
    }
    return ESP_OK;
}
/* Only fixed provider hosts are used. No redirects, no unverified TLS, no
 * response/body/URL logging (query strings contain provider credentials). */
static bool get_json(const char *url,response_t *response,int *status)
{
    response->used=0;response->invalid=false;response->retry_after=0;
    response->deadline=esp_timer_get_time()+25000000;
    esp_http_client_config_t config={
        .url=url,.event_handler=http_event,.user_data=response,
        .crt_bundle_attach=esp_crt_bundle_attach,.timeout_ms=12000,
        .disable_auto_redirect=true,.buffer_size=2048,.buffer_size_tx=2048,
        .user_agent="ESP-Home-Panel/Weather",.keep_alive_enable=false,
    };
    esp_http_client_handle_t client=esp_http_client_init(&config);
    *status=0;if(!client)return false;
    esp_http_client_set_header(client,"Accept","application/json");
    esp_http_client_set_header(client,"Accept-Encoding","identity");
    esp_err_t result=esp_http_client_perform(client);
    *status=esp_http_client_get_status_code(client);
    bool complete=esp_http_client_is_complete_data_received(client);
    esp_http_client_cleanup(client);
    response->body[response->used]=0;
    return result==ESP_OK&&complete&&!response->invalid&&*status==200&&response->used>0;
}
static void error_text(char *out,size_t size,int status,bool transport)
{
    if(status==401||status==403)snprintf(out,size,"API key rejected or service not included (HTTP %d)",status);
    else if(status==429)snprintf(out,size,"Provider rate limit; waiting before retry");
    else if(status && status!=200)snprintf(out,size,"Provider returned HTTP %d",status);
    else if(!transport)snprintf(out,size,"HTTPS/network error or incomplete/oversize response");
    else snprintf(out,size,"Station not found, ambiguous, or invalid provider data");
}
static void weather_task(void *argument)
{
    (void)argument;
    /* Settings are immutable for this worker; Save reboots the device. */
    panel_settings_t *cfg=heap_caps_malloc(sizeof(*cfg),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    weather_snapshot_t *next=heap_caps_malloc(sizeof(*next),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    char *body=heap_caps_malloc(WEATHER_MAX_PAYLOAD+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!cfg||!next||!body){
        free(cfg);free(next);free(body);
        portENTER_CRITICAL(&s_lock);
        snprintf(s_weather->station_status,sizeof(s_weather->station_status),"Insufficient memory for weather client");++s_weather->revision;
        portEXIT_CRITICAL(&s_lock);vTaskDelete(NULL);return;
    }
    *cfg=*settings_get();
    char station[384],key[388],app[388],forecast_key[388],url[2048];
    weather_encode(station,sizeof(station),cfg->weather_station);
    weather_encode(key,sizeof(key),cfg->weather_api_key);
    weather_encode(app,sizeof(app),cfg->weather_app_key);
    weather_encode(forecast_key,sizeof(forecast_key),cfg->weather_forecast_key[0]?cfg->weather_forecast_key:cfg->weather_api_key);
    int64_t station_due=0,forecast_due=0;
    unsigned station_failures=0,forecast_failures=0;
    response_t response={.body=body};
    while(true){
        weather_copy(next);
        int64_t now=esp_timer_get_time();
        if(!network_is_connected()||time(NULL)<1704067200){
            const char *message=network_is_connected()?"Waiting for clock sync (HTTPS)":"Wi-Fi disconnected";
            if(strcmp(next->station_status,message)){snprintf(next->station_status,sizeof(next->station_status),"%s",message);publish(next);}
            vTaskDelay(pdMS_TO_TICKS(2000));continue;
        }
        if(!cfg->weather_station[0]||!cfg->weather_api_key[0]||(cfg->weather_provider==1&&!cfg->weather_app_key[0])){
            if(strcmp(next->station_status,"Configure provider, station and API keys")){snprintf(next->station_status,sizeof(next->station_status),"Configure provider, station and API keys");publish(next);}
            vTaskDelay(pdMS_TO_TICKS(5000));continue;
        }
        if(now>=station_due){
            if(cfg->weather_provider==1)snprintf(url,sizeof(url),"https://rt.ambientweather.net/v1/devices?apiKey=%s&applicationKey=%s",key,app);
            else snprintf(url,sizeof(url),"https://api.weather.com/v2/pws/observations/current?stationId=%s&format=json&units=m&numericPrecision=decimal&apiKey=%s",station,key);
            int status;bool fetched=get_json(url,&response,&status);
            double lat=next->latitude,lon=next->longitude;
            if(fetched&&weather_parse_station(next,body,response.used,cfg->weather_provider==1,cfg->weather_station,cfg->weather_units==1)){
                next->station_received_us=esp_timer_get_time();station_failures=0;
                station_due=esp_timer_get_time()+(int64_t)cfg->weather_poll_minutes*60000000;
                if(isfinite(next->latitude)&&isfinite(next->longitude)&&(!isfinite(lat)||!isfinite(lon)||fabs(lat-next->latitude)>0.0001||fabs(lon-next->longitude)>0.0001)){
                    next->forecast_available=false;next->forecast_received_us=0;next->day_count=next->hour_count=0;
                    forecast_due=0;forecast_failures=0;
                }
            }else{
                /* A parser may have modified its candidate; restore last good data. */
                weather_copy(next);next->station_available=false;
                error_text(next->station_status,sizeof(next->station_status),status,fetched);
                station_due=esp_timer_get_time()+(int64_t)weather_retry_seconds(station_failures++,status,response.retry_after)*1000000;
            }
            publish(next);
        }
        now=esp_timer_get_time();
        if(cfg->weather_forecast==2){
            if(strcmp(next->forecast_status,"Forecast disabled")){snprintf(next->forecast_status,sizeof(next->forecast_status),"Forecast disabled");publish(next);}
        }else if(!isfinite(next->latitude)||!isfinite(next->longitude)){
            if(strcmp(next->forecast_status,"Station coordinates not available")){snprintf(next->forecast_status,sizeof(next->forecast_status),"Station coordinates not available");publish(next);}
        }else if(now>=forecast_due){
            if(cfg->weather_forecast==1){
                snprintf(url,sizeof(url),"https://api.weather.com/v3/wx/forecast/daily/5day?geocode=%.6f,%.6f&units=m&language=en-US&format=json&apiKey=%s",next->latitude,next->longitude,forecast_key);
            }else{
                snprintf(url,sizeof(url),"https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f&daily=temperature_2m_max,temperature_2m_min,weather_code,precipitation_sum,precipitation_probability_max,wind_speed_10m_max,uv_index_max,sunrise,sunset&hourly=temperature_2m,precipitation_probability,precipitation,weather_code&temperature_unit=celsius&wind_speed_unit=kmh&precipitation_unit=mm&timeformat=unixtime&timezone=auto&forecast_days=5",next->latitude,next->longitude);
            }
            int status;bool fetched=get_json(url,&response,&status);
            if(fetched&&weather_parse_forecast(next,body,response.used,cfg->weather_forecast==1,time(NULL))){
                next->forecast_received_us=esp_timer_get_time();forecast_failures=0;
                forecast_due=esp_timer_get_time()+900000000;
            }else{
                weather_copy(next);next->forecast_available=false;
                error_text(next->forecast_status,sizeof(next->forecast_status),status,fetched);
                if(fetched)snprintf(next->forecast_status,sizeof(next->forecast_status),"Forecast response incomplete or unsupported");
                forecast_due=esp_timer_get_time()+(int64_t)weather_retry_seconds(forecast_failures++,status,response.retry_after)*1000000;
            }
            publish(next);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void weather_start(void)
{
    if(s_started||!settings_get()->weather_enabled||!weather_init())return;
    /* Suppress client diagnostics that can contain credential-bearing paths. */
    esp_log_level_set("HTTP_CLIENT",ESP_LOG_NONE);
    if(xTaskCreate(weather_task,"weather_https",8192,NULL,2,NULL)==pdPASS)s_started=true;
    else{
        portENTER_CRITICAL(&s_lock);
        snprintf(s_weather->station_status,sizeof(s_weather->station_status),"Unable to start weather worker");++s_weather->revision;
        portEXIT_CRITICAL(&s_lock);
    }
}
