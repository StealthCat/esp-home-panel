/* Compile the production UI unchanged against LVGL; only ESP hardware/MQTT are stubbed. */
#include <assert.h>
#include <stdlib.h>
#include "../../main/ui.c"

static panel_settings_t config;
static int64_t fake_now = 100000000;
static weather_snapshot_t demo_weather;
static presence_reading_t demo_presence;
static uint8_t backlight;
int64_t esp_timer_get_time(void) { return fake_now; }
bool board_7c_has_pwm_backlight(void) { return true; }
esp_err_t board_7c_set_backlight(uint8_t p) { backlight=p;return ESP_OK; }
presence_reading_t c4001_reading(void) { return demo_presence; }
ambient_light_reading_t sensors_ambient_light(void) { return (ambient_light_reading_t){0}; }
bool weather_init(void) { return true; }
bool weather_copy(weather_snapshot_t *out) { *out=demo_weather;return true; }
static lv_indev_t *test_input;
static lv_point_t input_point;
static bool input_pressed;
static void read_touch(lv_indev_t *input,lv_indev_data_t *data) {
    (void)input;data->point=input_point;data->state=input_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
static void touch_at(int x,int y,bool pressed) {
    input_point=(lv_point_t){x,y};input_pressed=pressed;
    fake_now+=30000;lv_tick_inc(30);lv_indev_read(test_input);
}

static uint16_t framebuffer[800 * 480];
static unsigned flush_count;
static int tapo_commands, fan_commands, echo_commands;
static bool last_playing, last_next;
static uint32_t last_rgb;
const panel_settings_t *settings_get(void) { return &config; }
void mqtt_bridge_tapo_power(bool on) {(void)on;tapo_commands++;}
void mqtt_bridge_tapo_brightness(uint8_t p) {(void)p;tapo_commands++;}
void mqtt_bridge_tapo_rgb(uint8_t r,uint8_t g,uint8_t b) {last_rgb=((uint32_t)r<<16)|((uint32_t)g<<8)|b;tapo_commands++;}
void mqtt_bridge_overhead_power(bool on) {(void)on;}
void mqtt_bridge_overhead_brightness(uint8_t p) {(void)p;}
void mqtt_bridge_fan_power(bool on) {(void)on;fan_commands++;}
void mqtt_bridge_echo_volume(uint8_t p) {(void)p;echo_commands++;}
void mqtt_bridge_echo_mute(bool m,uint8_t p) {(void)m;(void)p;echo_commands++;}
void mqtt_bridge_echo_play_pause(bool p) {last_playing=p;echo_commands++;}
void mqtt_bridge_echo_track(bool next) {last_next=next;echo_commands++;}

static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *pixels) {
    (void)area;(void)pixels;flush_count++;lv_display_flush_ready(d);
}
lv_display_t *wokwi_panel_create_display(void) {
    lv_display_t *d=lv_display_create(800,480);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,framebuffer,NULL,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d,flush);
    return d;
}
esp_err_t wokwi_panel_add_touch(lv_display_t *d) {
    test_input=lv_indev_create();lv_indev_set_type(test_input,LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(test_input,d);lv_indev_set_read_cb(test_input,read_touch);return ESP_OK;
}

static void assert_image_tree(lv_obj_t *parent) {
    for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++) {
        lv_obj_t *obj=lv_obj_get_child(parent,i);
        if(lv_obj_check_type(obj,&lv_image_class)) {
            const lv_image_dsc_t *src=lv_image_get_src(obj);
            assert(src && src->data && src->data_size==src->header.w*src->header.h*4);
            assert(!lv_obj_has_flag(obj,LV_OBJ_FLAG_CLICKABLE));
            lv_area_t a,p;lv_obj_get_coords(obj,&a);lv_obj_get_coords(parent,&p);
            if(!(a.x1>=p.x1 && a.y1>=p.y1 && a.x2<=p.x2 && a.y2<=p.y2)) fprintf(stderr,"Clipped image %ux%u [%d,%d,%d,%d] parent [%d,%d,%d,%d]\n",src->header.w,src->header.h,a.x1,a.y1,a.x2,a.y2,p.x1,p.y1,p.x2,p.y2);
            assert(a.x1>=p.x1 && a.y1>=p.y1 && a.x2<=p.x2 && a.y2<=p.y2);
        }
        assert_image_tree(obj);
    }
}
int main(int argc,char **argv) {
    if(argc<3 || argc>4) {fprintf(stderr,"usage: panel_render THEME OUTPUT.ppm\n");return 2;}
    const uint32_t custom_colors[PANEL_COLOR_PRESET_COUNT] = {
        0x123456, 0xFFFFFF, 0x000000, 0x00FF00, 0xABCDEF, 0xE83691
    };
    memcpy(config.color_presets, custom_colors, sizeof(custom_colors));
    config.theme=(uint8_t)atoi(argv[1]);
    config.backlight_percent=75;
    config.weather_enabled=1;
    config.touch_wake_minutes=2;
    config.sleep_delay_seconds=60;
    assert(ui_start(NULL,NULL)==ESP_OK);
    assert(strcmp(lv_label_get_text(s_tapo_status),"Waiting")==0);
    for (int i = 0; i < PANEL_COLOR_PRESET_COUNT; ++i) {
        uint32_t rgb = custom_colors[i];
        assert((lv_color_to_u32(lv_obj_get_style_bg_color(s_color_presets[i], 0)) & 0xFFFFFF) == rgb);
        lv_obj_send_event(s_color_presets[i], LV_EVENT_CLICKED, NULL);
        assert(last_rgb == rgb);
        assert(color_preset_match(rgb >> 16, rgb >> 8, rgb) == i);
        assert(lv_obj_get_style_border_width(s_color_presets[i], 0) == 3);
    }
    assert(tapo_commands == PANEL_COLOR_PRESET_COUNT);
    tapo_commands = 0;
    ui_set_network_online(true);ui_set_mqtt_online(true);
    ui_apply_tapo_state(true,80,0,255,0);ui_apply_overhead_state(true,60);
    ui_apply_fan_state(true);ui_apply_echo_state(70,false,false,"Midnight City","M83");
    pending_state_timer_cb(NULL);
    assert(lv_slider_get_value(s_tapo_slider)==80);
    assert(lv_slider_get_value(s_overhead_slider)==60);
    assert(lv_slider_get_value(s_echo_slider)==70);
    assert(s_alexa_now_playing && s_alexa_artist);
    assert(strcmp(lv_label_get_text(s_alexa_now_playing),"Midnight City")==0);
    assert(strcmp(lv_label_get_text(s_alexa_artist),"M83")==0);
    ui_apply_echo_state(70,false,true,"A long track title that must scroll within the card without covering neighboring artwork", "A long author or artist name that must stay on the second line");
    pending_state_timer_cb(NULL);
    assert(lv_label_get_long_mode(s_alexa_now_playing)==LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    assert(lv_label_get_long_mode(s_alexa_artist)==LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    ui_apply_echo_state(70,false,false,"", "");pending_state_timer_cb(NULL);
    assert(strcmp(lv_label_get_text(s_alexa_now_playing),"Nothing playing")==0);
    assert(strcmp(lv_label_get_text(s_alexa_artist),"")==0);
    assert(lv_obj_has_state(s_fan_switch,LV_STATE_CHECKED));
    assert(strcmp(lv_label_get_text(s_tapo_status),"Online")==0);
    assert(strcmp(lv_label_get_text(s_fan_state_label),"Fan is On")==0);
    lv_obj_send_event(s_color_presets[3],LV_EVENT_CLICKED,NULL);
    lv_obj_send_event(s_fan_switch,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_send_event(s_echo_play,LV_EVENT_CLICKED,NULL);
    assert(tapo_commands==1 && fan_commands==1 && echo_commands==1 && last_playing);
    lv_obj_send_event(s_echo_play,LV_EVENT_CLICKED,NULL);assert(last_playing);
    lv_obj_send_event(s_echo_pause,LV_EVENT_CLICKED,NULL);assert(!last_playing);
    lv_obj_send_event(s_echo_pause,LV_EVENT_CLICKED,NULL);assert(!last_playing);
    lv_obj_send_event(s_echo_previous,LV_EVENT_CLICKED,NULL);assert(!last_next);
    lv_obj_send_event(s_echo_next,LV_EVENT_CLICKED,NULL);assert(last_next);
    assert(echo_commands==6);
    ui_set_mqtt_online(false);pending_state_timer_cb(NULL);
    assert(strcmp(lv_label_get_text(s_echo_status),"Offline")==0);
    ui_set_mqtt_online(true);ui_apply_tapo_state(true,80,0,255,0);
    ui_apply_overhead_state(true,60);ui_apply_fan_state(true);
    ui_apply_echo_state(70,false,false,"Midnight City","M83");pending_state_timer_cb(NULL);
    lv_label_set_text(s_clock,"10:24");lv_label_set_text(s_clock_period,"am");lv_label_set_text(s_date,"Sat, Apr 26");
    align_clock_labels();
    lv_obj_update_layout(lv_screen_active());
    assert_image_tree(lv_screen_active());
    /* Real pointer input: a sleeping tap over a switch must only wake. */
    config.sleep_enabled=config.presence_enabled=1;
    demo_presence=(presence_reading_t){true,false,fake_now};
    display_timer_cb(NULL);
    fake_now+=200000000;lv_tick_inc(200000);
    demo_presence.sampled_us=fake_now;
    display_timer_cb(NULL);
    assert(s_display_policy.asleep && backlight==0);
    lv_obj_update_layout(lv_layer_top());
    int prior=tapo_commands;
    touch_at(344,107,true);display_timer_cb(NULL);
    assert(backlight>0);
    touch_at(344,107,false);display_timer_cb(NULL);
    assert(tapo_commands==prior);
    assert(lv_obj_has_flag(s_wake_shield,LV_OBJ_FLAG_HIDDEN));
    touch_at(344,107,true);touch_at(344,107,false);
    assert(tapo_commands==prior+1);
    /* Header taps and horizontal gestures navigate; lower controls do not. */
    touch_at(300,30,true);touch_at(300,30,false);assert(s_show_weather);assert(!strcmp(lv_label_get_text(s_page_indicator),"Weather"));
    touch_at(550,30,true);touch_at(300,30,true);touch_at(200,30,false);
    assert(!s_show_weather);assert(!strcmp(lv_label_get_text(s_page_indicator),"Controls"));
    char clock_text[24];struct tm clock_tm={.tm_year=126,.tm_mon=9,.tm_mday=11,.tm_hour=0,.tm_min=5,.tm_isdst=-1};
    weather_clock(clock_text,sizeof(clock_text),mktime(&clock_tm));assert(!strcmp(clock_text,"12:05 am"));
    clock_tm.tm_hour=12;weather_clock(clock_text,sizeof(clock_text),mktime(&clock_tm));assert(!strcmp(clock_text,"12:05 pm"));
    clock_tm.tm_hour=19;weather_clock(clock_text,sizeof(clock_text),mktime(&clock_tm));assert(!strcmp(clock_text,"7:05 pm"));
    config.sleep_enabled=0;
    weather_reset(&demo_weather);
    demo_weather.revision=1;
    demo_weather.station_available=demo_weather.forecast_available=true;
    demo_weather.station_received_us=demo_weather.forecast_received_us=fake_now;
    demo_weather.observed_at=time(NULL)-45;
    strcpy(demo_weather.name,"Evans Weather");
    demo_weather.current=(weather_current_t){.temp=24.7,.humidity=62,.feels=25.8,.dew=17,.heat_index=25.8,.wind_chill=NAN,.wind=7.7,.gust=14.8,.direction=315,.pressure=1018,.rain_rate=0,.rain_day=3.5,.uv=4,.solar=386};
    const char *labels[]={"Outdoor temperature","Humidity","Wind speed","Wind gust","Rain today","Relative pressure","UV index","Solar radiation","Indoor temperature","Indoor humidity","Rain this month","Station battery"};
    const char *values[]={"76.5 °F","62%","4.8 mph","9.2 mph","0.14 in","30.06 inHg","4","386 W/m2","72.1 °F","48%","2.38 in","Normal"};
    demo_weather.field_count=12;
    for(int i=0;i<12;i++) {snprintf(demo_weather.fields[i].label,sizeof(demo_weather.fields[i].label),"%s",labels[i]);snprintf(demo_weather.fields[i].value,sizeof(demo_weather.fields[i].value),"%s",values[i]);}
    weather_icon_t icons[]={WX_PARTLY,WX_CLEAR,WX_RAIN,WX_STORM,WX_CLOUD};
    demo_weather.day_count=5;
    for(int i=0;i<5;i++) {
        demo_weather.days[i]=(weather_day_t){.date=time(NULL)+i*86400,.sunrise=time(NULL)-14400,.sunset=time(NULL)+21600,.icon=icons[i],.high=26+i,.low=14+i,.rain_chance=i*20,.rain=i*2,.wind=15+i,.uv=4};
        strcpy(demo_weather.days[i].narrative,"Clouds giving way to sunshine. A light northwest breeze with a chance of showers later in the day.");
    }
    demo_weather.hour_count=12;
    for(int i=0;i<12;i++)demo_weather.hours[i]=(weather_hour_t){.time=time(NULL)+i*3600,.temp=24+4*sinf(i*0.4),.rain_chance=i*7,.rain=i*0.2};
    update_weather_view();
    assert(strstr(lv_label_get_text(s_weather_summary),"76.5"));
    /* Warning labels/palette match HeatIndexDisplay, including exact boundaries. */
    const float heat_levels[]={79.9f,80,89.9f,90,102.9f,103,124.9f,125};
    const char *heat_labels[]={"NORMAL","CAUTION","CAUTION","EXTREME CAUTION","EXTREME CAUTION","DANGER","DANGER","EXTREME DANGER"};
    const float cold_levels[]={21,20,0,-20,-35};
    const char *cold_labels[]={"CHILLY","COLD","VERY COLD","DANGER","EXTREME DANGER"};
    for(unsigned i=0;i<8;i++)assert(!strcmp(weather_risk_style(heat_levels[i],false).label,heat_labels[i]));
    for(unsigned i=0;i<5;i++)assert(!strcmp(weather_risk_style(cold_levels[i],true).label,cold_labels[i]));
    assert(weather_risk_style(-48,true).accent==0xdbacff);
    assert(weather_risk_style(-32,true).accent==0x8bc6ff);
    assert(weather_risk_style(-18,true).accent==0xc2ebff);
    assert(!strcmp(weather_risk_style(NAN,false).label,"UNAVAILABLE"));
    weather_current_t normal=demo_weather.current;
    demo_weather.current.temp=10;demo_weather.current.wind=3*1.609344f;
    assert(!weather_wind_chill_applies(&demo_weather.current));
    demo_weather.current.wind=5;demo_weather.current.wind_chill=-30;
    demo_weather.revision++;update_weather_view();
    assert(!strcmp(lv_label_get_text(s_risk_title),"WIND CHILL"));
    assert(!strcmp(lv_label_get_text(s_risk_label),"DANGER"));
    demo_weather.current.wind=NAN;
    assert(!weather_wind_chill_applies(&demo_weather.current));
    demo_weather.current=normal;demo_weather.current.heat_index=NAN;
    demo_weather.revision++;update_weather_view();
    assert(!strcmp(lv_label_get_text(s_risk_label),"UNAVAILABLE"));
    assert(!strcmp(lv_label_get_text(s_risk),"--"));
    demo_weather.current=normal;demo_weather.current.heat_index=35;
    demo_weather.revision++;update_weather_view();lv_obj_update_layout(lv_screen_active());
    assert(!strcmp(lv_label_get_text(s_risk_title),"HEAT INDEX"));
    assert(!strcmp(lv_label_get_text(s_risk_label),"EXTREME CAUTION"));
    lv_point_t pill_size;
    lv_text_get_size(&pill_size,lv_label_get_text(s_risk_label),&lv_font_montserrat_14,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    assert(pill_size.x<=lv_obj_get_width(s_risk_label));
    demo_weather.current=normal;demo_weather.revision++;update_weather_view();
    config.weather_poll_minutes=30;
    fake_now+=600000000;update_weather_view();
    assert(!strstr(lv_label_get_text(s_weather_status),"Stale"));
    config.weather_poll_minutes=1;update_weather_view();
    assert(strstr(lv_label_get_text(s_weather_status),"Stale"));
    strcpy(demo_weather.station_status,"API key rejected");demo_weather.revision++;
    update_weather_view();assert(strstr(lv_label_get_text(s_weather_status),"Cached"));
    demo_weather.station_status[0]=0;fake_now-=600000000;
    demo_weather.current.temp=42;demo_weather.revision++;update_weather_view();
    lv_obj_update_layout(lv_screen_active());
    lv_point_t text_size;
    lv_text_get_size(&text_size,lv_label_get_text(s_weather_summary),lv_obj_get_style_text_font(s_weather_summary,0),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    assert(text_size.x<=lv_obj_get_width(s_weather_summary));
    demo_weather.current.temp=24.7;demo_weather.revision++;update_weather_view();
    /* Stable data must not redraw or recreate forecast widgets. */
    set_page(true);weather_section(1);lv_obj_update_layout(lv_screen_active());lv_refr_now(NULL);
    lv_obj_t *day_label=s_day_highs[0];unsigned before_flush=flush_count;
    for(unsigned i=0;i<8;i++){update_weather_view();lv_refr_now(NULL);}
    assert(flush_count==before_flush);
    demo_weather.revision++;update_weather_view();lv_refr_now(NULL);
    assert(flush_count==before_flush && s_day_highs[0]==day_label);
    demo_weather.days[0].high+=1;demo_weather.revision++;update_weather_view();lv_refr_now(NULL);
    assert(flush_count>before_flush && s_day_highs[0]==day_label);
    assert(strstr(lv_label_get_text(s_tomorrow),"High ") && strstr(lv_label_get_text(s_tomorrow),"Low "));
    demo_weather.days[0].high-=1;demo_weather.revision++;update_weather_view();
    weather_section(0);clock_timer_cb(NULL);lv_refr_now(NULL);before_flush=flush_count;
    clock_timer_cb(NULL);ui_set_network_online(true);pending_state_timer_cb(NULL);lv_refr_now(NULL);
    assert(flush_count==before_flush);
    demo_weather.revision++;update_weather_view();lv_refr_now(NULL);
    assert(flush_count==before_flush);
    lv_label_set_text(s_clock,"10:24");lv_label_set_text(s_clock_period,"am");lv_label_set_text(s_date,"Sat, Apr 26");align_clock_labels();
    set_page(argc==4);
    if(argc==4 && !strcmp(argv[3],"forecast"))weather_section(1);
    if(argc==4 && !strcmp(argv[3],"heat")){
        demo_weather.current.temp=35;demo_weather.current.heat_index=41;demo_weather.current.feels=41;demo_weather.revision++;update_weather_view();
    }
    if(argc==4 && !strcmp(argv[3],"cold")){
        demo_weather.current.temp=-25;demo_weather.current.wind=25;demo_weather.current.wind_chill=-39;demo_weather.current.feels=-39;
        demo_weather.revision++;update_weather_view();
    }
    if(argc==4 && !strcmp(argv[3],"forecast-metric")){
        config.weather_units=1;
        for(unsigned i=0;i<WEATHER_HOURS;i++)demo_weather.hours[i].temp=-12+(int)i;
        demo_weather.hours[5].temp=NAN;
        demo_weather.revision++;update_weather_view();weather_section(1);
    }
    if(argc==4 && !strcmp(argv[3],"station"))weather_section(2);
    if(argc==4 && !strcmp(argv[3],"day")){weather_section(1);lv_obj_send_event(s_forecast_cards[0],LV_EVENT_CLICKED,NULL);}
    lv_obj_update_layout(lv_screen_active());
    assert_image_tree(lv_screen_active());
    lv_refr_now(NULL);
    FILE *out=fopen(argv[2],"wb");assert(out);
    fprintf(out,"P6\n800 480\n255\n");
    for(int i=0;i<800*480;i++) {
        uint16_t v=framebuffer[i];
        uint8_t rgb[3]={(uint8_t)(((v>>11)&31)*255/31),(uint8_t)(((v>>5)&63)*255/63),(uint8_t)((v&31)*255/31)};
        fwrite(rgb,1,3,out);
    }
    fclose(out);printf("Theme %u: render and control/state checks passed\n",config.theme);
    return 0;
}
