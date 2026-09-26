/* Compile the production UI unchanged against LVGL; only ESP hardware/MQTT are stubbed. */
#include <assert.h>
#include <stdlib.h>
#include "../../main/ui.c"

static panel_settings_t config;
static uint16_t framebuffer[800 * 480];
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
    (void)area;(void)pixels;lv_display_flush_ready(d);
}
lv_display_t *wokwi_panel_create_display(void) {
    lv_display_t *d=lv_display_create(800,480);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d,framebuffer,NULL,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d,flush);
    return d;
}
esp_err_t wokwi_panel_add_touch(lv_display_t *d) {(void)d;return ESP_OK;}

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
    if(argc!=3) {fprintf(stderr,"usage: panel_render THEME OUTPUT.ppm\n");return 2;}
    const uint32_t custom_colors[PANEL_COLOR_PRESET_COUNT] = {
        0x123456, 0xFFFFFF, 0x000000, 0x00FF00, 0xABCDEF, 0xE83691
    };
    memcpy(config.color_presets, custom_colors, sizeof(custom_colors));
    config.theme=(uint8_t)atoi(argv[1]);
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
