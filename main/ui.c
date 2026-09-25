#include "ui.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "board_7c.h"
#include "lvgl.h"
#include "mqtt_bridge.h"
#include "ui_theme.h"
#include "theme_art.h"
#ifdef WOKWI_EMULATOR
#include "wokwi_panel.h"
#endif

static const char *TAG = "ui";

static const panel_theme_t *T;

static lv_display_t *s_disp;
static bool s_artwork_mode = false;

static lv_obj_t *s_wifi_icon;
static lv_obj_t *s_mqtt_dot;
static lv_obj_t *s_clock;
static lv_obj_t *s_date;

static lv_obj_t *s_tapo_switch;
static lv_obj_t *s_tapo_slider;
static lv_obj_t *s_tapo_pct;
static lv_obj_t *s_tapo_status;
static lv_obj_t *s_colorwheel;
static lv_obj_t *s_color_knob;
static lv_obj_t *s_color_presets[6];
static void *s_colorwheel_buf;

static const uint32_t s_color_preset_display[6] = {
    0xFF3B30, 0xFF9500, 0xFFD60A, 0x30D158, 0x0A84FF, 0xBF5AF2
};
static const uint32_t s_color_preset_rgb[6] = {
    0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x0066FF, 0xA020F0
};

static lv_obj_t *s_overhead_switch;
static lv_obj_t *s_overhead_slider;
static lv_obj_t *s_overhead_pct;
static lv_obj_t *s_overhead_status;

static lv_obj_t *s_fan_switch;
static lv_obj_t *s_fan_icon;
static lv_obj_t *s_fan_status;
static lv_obj_t *s_fan_state_label;

static lv_obj_t *s_echo_slider;
static lv_obj_t *s_echo_pct;
static lv_obj_t *s_echo_status;
static lv_obj_t *s_echo_mute;
static lv_obj_t *s_echo_play;
static lv_obj_t *s_alexa_now_playing;
static lv_obj_t *s_alexa_artist;

static bool s_syncing;
static bool s_fan_on;
static bool s_echo_playing;
static bool s_echo_muted;
static uint8_t s_echo_unmuted_volume = 50;
static int s_fan_angle;

enum {
    UI_PENDING_NETWORK  = 1u << 0,
    UI_PENDING_MQTT     = 1u << 1,
    UI_PENDING_TAPO     = 1u << 2,
    UI_PENDING_OVERHEAD = 1u << 3,
    UI_PENDING_FAN      = 1u << 4,
    UI_PENDING_ALEXA    = 1u << 5,
};

typedef struct {
    uint32_t dirty;

    bool network_online;
    bool mqtt_online;

    bool tapo_on;
    uint8_t tapo_brightness;
    uint8_t tapo_r;
    uint8_t tapo_g;
    uint8_t tapo_b;

    bool overhead_on;
    uint8_t overhead_brightness;

    bool fan_on;

    uint8_t alexa_volume;
    bool alexa_muted;
    bool alexa_playing;
    char alexa_title[96];
    char alexa_artist[64];
} pending_ui_state_t;

static pending_ui_state_t s_pending_ui;
static portMUX_TYPE s_pending_ui_mux = portMUX_INITIALIZER_UNLOCKED;

static void discard_pending_ui(uint32_t mask)
{
    portENTER_CRITICAL(&s_pending_ui_mux);
    s_pending_ui.dirty &= ~mask;
    portEXIT_CRITICAL(&s_pending_ui_mux);
}

static lv_style_t s_card_style;
static lv_style_t s_button_style;
static lv_style_t s_button_pressed_style;

static inline lv_color_t c(uint32_t hex)
{
    return lv_color_hex(hex);
}

static void style_screen(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, c(T->screen_bg), 0);
    lv_obj_set_style_bg_grad_color(screen, c(T->screen_bg_2), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

static void init_styles(void)
{
    lv_style_init(&s_card_style);
    lv_style_set_radius(&s_card_style, 14);
    lv_style_set_bg_color(&s_card_style, c(T->card_bg));
    lv_style_set_bg_grad_color(&s_card_style, c(T->card_bg_2));
    lv_style_set_bg_grad_dir(&s_card_style, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&s_card_style, 238);
    lv_style_set_border_width(&s_card_style, 1);
    lv_style_set_border_color(&s_card_style, c(T->card_border));
    lv_style_set_border_opa(&s_card_style, T->magic ? 90 : 125);
    lv_style_set_shadow_width(&s_card_style, 8);
    lv_style_set_shadow_spread(&s_card_style, 0);
    lv_style_set_shadow_color(&s_card_style, c(T->accent));
    lv_style_set_shadow_opa(&s_card_style, T->kittens ? 28 : 18);
    lv_style_set_pad_all(&s_card_style, 0);

    lv_style_init(&s_button_style);
    lv_style_set_radius(&s_button_style, 10);
    lv_style_set_bg_color(&s_button_style, c(T->card_bg_2));
    lv_style_set_bg_opa(&s_button_style, 212);
    lv_style_set_border_width(&s_button_style, 1);
    lv_style_set_border_color(&s_button_style, c(T->card_border));
    lv_style_set_border_opa(&s_button_style, 130);
    lv_style_set_text_color(&s_button_style, c(T->text));
    lv_style_set_shadow_width(&s_button_style, 3);
    lv_style_set_shadow_color(&s_button_style, c(T->accent));
    lv_style_set_shadow_opa(&s_button_style, 18);

    lv_style_init(&s_button_pressed_style);
    lv_style_set_bg_color(&s_button_pressed_style, c(T->accent));
    lv_style_set_bg_opa(&s_button_pressed_style, 180);
    lv_style_set_transform_width(&s_button_pressed_style, -2);
    lv_style_set_transform_height(&s_button_pressed_style, -2);
}

static lv_obj_t *add_theme_sprite(lv_obj_t *parent, theme_art_id_t id,
                                  int x, int y, bool background);

static void add_magic_card_ornaments(lv_obj_t *card, int w, int h)
{
    const int s = 36;
    add_theme_sprite(card, THEME_ART_MAGIC_CORNER_TL, 0, 0, false);
    add_theme_sprite(card, THEME_ART_MAGIC_CORNER_TR, w - s, 0, false);
    add_theme_sprite(card, THEME_ART_MAGIC_CORNER_BL, 0, h - s, false);
    add_theme_sprite(card, THEME_ART_MAGIC_CORNER_BR, w - s, h - s, false);
}

static lv_obj_t *add_theme_sprite(lv_obj_t *parent, theme_art_id_t id,
                                  int x, int y, bool background)
{
    const lv_image_dsc_t *src = theme_art_get(id);
    if (!src || !src->data || src->data_size == 0) {
        return NULL;
    }

    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src);
    lv_obj_set_pos(img, x, y);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    if (background) {
        lv_obj_move_background(img);
    }
    return img;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y,
                            const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, c(color), 0);
    if (s_artwork_mode) {
        lv_obj_set_style_text_opa(label, LV_OPA_TRANSP, 0);
    }
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_add_style(card, &s_card_style, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    if (T->magic) {
        add_magic_card_ornaments(card, w, h);
    }
    return card;
}

static lv_obj_t *make_switch(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_pos(sw, x, y);
    lv_obj_set_size(sw, 56, 30);
    lv_obj_set_style_bg_color(sw, c(0x33475B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, c(T->accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, c(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_shadow_width(sw, 4, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(sw, 80, LV_PART_KNOB);
    return sw;
}

static lv_obj_t *make_slider(lv_obj_t *parent, int x, int y, int w, uint32_t accent)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, w, 16);
    lv_slider_set_range(slider, 0, 100);

    lv_obj_set_style_bg_color(slider, c(0x3B4A58), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, 180, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 10, LV_PART_MAIN);

    lv_obj_set_style_bg_color(slider, c(accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 10, LV_PART_INDICATOR);

    lv_obj_set_style_bg_color(slider, c(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 4, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(slider, c(accent), LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, 80, LV_PART_KNOB);
    return slider;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_style(btn, &s_button_style, 0);
    lv_obj_add_style(btn, &s_button_pressed_style, LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, c(T->text), 0);
    if (s_artwork_mode) {
        lv_obj_set_style_text_opa(label, LV_OPA_TRANSP, 0);
    }
    lv_obj_center(label);
    return btn;
}

static void set_switch(lv_obj_t *sw, bool on)
{
    if (!sw) return;
    if (on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(sw, LV_STATE_CHECKED);
    }
}

static void set_percent_label(lv_obj_t *label, int value)
{
    if (!label) return;
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    lv_label_set_text_fmt(label, "%d%%", value);
}

static void add_circle(lv_obj_t *parent, int x, int y, int d, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *add_decor_box(lv_obj_t *parent, int x, int y, int w, int h,
                               uint32_t color, lv_opa_t opa, int radius);
static void add_paw(lv_obj_t *parent, int x, int y, uint32_t color, lv_opa_t opa)
{
    add_circle(parent, x + 8, y + 12, 10, color, opa);
    add_circle(parent, x + 2, y + 2, 6, color, opa);
    add_circle(parent, x + 10, y, 6, color, opa);
    add_circle(parent, x + 18, y + 3, 6, color, opa);
}

static lv_obj_t *add_decor_box(lv_obj_t *parent, int x, int y, int w, int h,
                               uint32_t color, lv_opa_t opa, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, c(color), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void add_chain_icon(lv_obj_t *parent, int x, int y, uint32_t color)
{
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *link = lv_obj_create(parent);
        lv_obj_set_pos(link, x + i * 13, y + (i ? 4 : 0));
        lv_obj_set_size(link, 25, 12);
        lv_obj_set_style_radius(link, 7, 0);
        lv_obj_set_style_bg_opa(link, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(link, 3, 0);
        lv_obj_set_style_border_color(link, c(color), 0);
        lv_obj_set_style_border_opa(link, 235, 0);
        lv_obj_set_style_pad_all(link, 0, 0);
        lv_obj_set_style_transform_rotation(link, i ? 3250 : 350, 0);
        lv_obj_remove_flag(link, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(link, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void add_bulb_icon(lv_obj_t *parent, int x, int y, uint32_t color)
{
    add_circle(parent, x + 2, y, 22, color, 235);
    add_decor_box(parent, x + 8, y + 21, 11, 8, color, 230, 2);
    add_decor_box(parent, x + 10, y + 31, 7, 3, color, 190, 1);
}

static void add_mini_fan_icon(lv_obj_t *parent, int x, int y, uint32_t color)
{
    /* Compact four-blade fan matching the mockup title artwork. */
    add_circle(parent, x + 13, y + 13, 8, color, 245);

    add_decor_box(parent, x + 11, y,      10, 15, color, 225, 7);
    add_decor_box(parent, x + 19, y + 11, 15, 10, color, 225, 7);
    add_decor_box(parent, x + 11, y + 19, 10, 15, color, 225, 7);
    add_decor_box(parent, x,      y + 11, 15, 10, color, 225, 7);
}


static void add_soccer_decor(lv_obj_t *screen)
{
    for (int i = 0; i < 10; ++i) {
        lv_obj_t *stripe = add_decor_box(
            screen, -40 + i * 95, 72, 48, 408,
            (i % 2) ? 0x0A3A20 : 0x0D4727, 28, 0);
        lv_obj_set_style_transform_rotation(stripe, 70, 0);
    }

    lv_obj_t *pitch = lv_obj_create(screen);
    lv_obj_set_pos(pitch, 0, 72);
    lv_obj_set_size(pitch, 800, 408);
    lv_obj_set_style_bg_opa(pitch, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pitch, 1, 0);
    lv_obj_set_style_border_color(pitch, c(0x50C878), 0);
    lv_obj_set_style_border_opa(pitch, 34, 0);
    lv_obj_set_style_radius(pitch, 0, 0);
    lv_obj_set_style_pad_all(pitch, 0, 0);
    lv_obj_remove_flag(pitch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(pitch, LV_OBJ_FLAG_CLICKABLE);

    add_decor_box(pitch, 399, 0, 2, 408, 0x50C878, 28, 0);

    lv_obj_t *center = lv_obj_create(pitch);
    lv_obj_set_pos(center, 330, 130);
    lv_obj_set_size(center, 140, 140);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center, 2, 0);
    lv_obj_set_style_border_color(center, c(0x50C878), 0);
    lv_obj_set_style_border_opa(center, 28, 0);
    lv_obj_remove_flag(center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(center, LV_OBJ_FLAG_CLICKABLE);

    /* The original soccer header ball overlaps the old settings gear in the
     * source art, so do not render that crop. Keep the header clean. */
}

static void add_kittens_decor(lv_obj_t *screen)
{
    add_theme_sprite(screen, THEME_ART_KITTEN_PEEK, 22, 64, false);
    add_theme_sprite(screen, THEME_ART_KITTEN_PAWS, 18, 398, false);
}

static void add_magic_decor(lv_obj_t *screen)
{
    add_theme_sprite(screen, THEME_ART_MAGIC_CASTLE, 392, 0, true);
}

static void add_classic_decor(lv_obj_t *screen)
{
    for (int i = 0; i < 6; ++i) {
        lv_obj_t *line = lv_obj_create(screen);
        lv_obj_set_pos(line, 420 + i * 55, 63);
        lv_obj_set_size(line, 42, 2);
        lv_obj_set_style_bg_color(line, c(T->accent), 0);
        lv_obj_set_style_bg_opa(line, 25 + i * 8, 0);
        lv_obj_set_style_border_width(line, 0, 0);
    }
}

static void add_theme_decor(lv_obj_t *screen)
{
    if (T->soccer) add_soccer_decor(screen);
    else if (T->kittens) add_kittens_decor(screen);
    else if (T->magic) add_magic_decor(screen);
    else add_classic_decor(screen);
}

static void switch_tapo_cb(lv_event_t *e)
{
    if (s_syncing) return;
    discard_pending_ui(UI_PENDING_TAPO);
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    ESP_LOGI(TAG, "UI CMD tapo power=%d", on);
    mqtt_bridge_tapo_power(on);
}

static void switch_overhead_cb(lv_event_t *e)
{
    if (s_syncing) return;
    discard_pending_ui(UI_PENDING_OVERHEAD);
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    ESP_LOGI(TAG, "UI CMD overhead power=%d", on);
    mqtt_bridge_overhead_power(on);
}

static void switch_fan_cb(lv_event_t *e)
{
    if (s_syncing) return;
    discard_pending_ui(UI_PENDING_FAN);
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    s_fan_on = on;
    if (s_fan_state_label) {
        lv_label_set_text(s_fan_state_label, on ? "Fan is On" : "Fan is Off");
    }
    ESP_LOGI(TAG, "UI CMD fan power=%d", on);
    mqtt_bridge_fan_power(on);
}

static void tapo_brightness_cb(lv_event_t *e)
{
    if (s_syncing) return;
    discard_pending_ui(UI_PENDING_TAPO);
    int value = lv_slider_get_value(lv_event_get_target(e));
    set_percent_label(s_tapo_pct, value);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        mqtt_bridge_tapo_brightness((uint8_t)value);
    }
}

static void overhead_brightness_cb(lv_event_t *e)
{
    if (s_syncing) return;
    discard_pending_ui(UI_PENDING_OVERHEAD);
    int value = lv_slider_get_value(lv_event_get_target(e));
    set_percent_label(s_overhead_pct, value);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        mqtt_bridge_overhead_brightness((uint8_t)value);
    }
}

static void echo_volume_cb(lv_event_t *e)
{
    if (s_syncing) return;
    discard_pending_ui(UI_PENDING_ALEXA);
    int value = lv_slider_get_value(lv_event_get_target(e));
    if (value > 0) {
        s_echo_unmuted_volume = (uint8_t)value;
    }
    set_percent_label(s_echo_pct, value);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        mqtt_bridge_echo_volume((uint8_t)value);
    }
}

static void colorwheel_position_knob(uint16_t hue, uint8_t sat);

static void color_preset_set_selected(int selected)
{
    for (int i = 0; i < 6; ++i) {
        lv_obj_t *btn = s_color_presets[i];
        if (!btn) continue;

        if (i == selected) {
            lv_obj_set_style_border_width(btn, 3, 0);
            lv_obj_set_style_border_color(btn, lv_color_white(), 0);
            lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_outline_width(btn, 2, 0);
            lv_obj_set_style_outline_color(btn, c(T->accent), 0);
            lv_obj_set_style_outline_opa(btn, LV_OPA_COVER, 0);
            lv_obj_set_style_outline_pad(btn, 2, 0);
        } else {
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_outline_width(btn, 0, 0);
        }
    }
}

static int color_preset_match(uint8_t r, uint8_t g, uint8_t b)
{
    int best = -1;
    unsigned best_dist = UINT_MAX;

    for (int i = 0; i < 6; ++i) {
        uint32_t rgb = s_color_preset_rgb[i];
        int pr = (rgb >> 16) & 0xFF;
        int pg = (rgb >> 8) & 0xFF;
        int pb = rgb & 0xFF;

        int dr = (int)r - pr;
        int dg = (int)g - pg;
        int db = (int)b - pb;
        unsigned dist = (unsigned)(dr * dr + dg * dg + db * db);

        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }

    /* Allow small HA/device color round-trip differences but don't claim a
     * preset for a clearly custom color. */
    return best_dist <= (18u * 18u * 3u) ? best : -1;
}

static void color_ui_apply_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    lv_color_hsv_t hsv = lv_color_rgb_to_hsv(r, g, b);
    colorwheel_position_knob(hsv.h, hsv.s);
    color_preset_set_selected(color_preset_match(r, g, b));
}

static void preset_color_cb(lv_event_t *e)
{
    discard_pending_ui(UI_PENDING_TAPO);

    uintptr_t packed = (uintptr_t)lv_event_get_user_data(e);
    int preset = (int)packed;
    if (preset < 0 || preset >= 6) return;

    uint32_t rgb = s_color_preset_rgb[preset];
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;

    color_ui_apply_rgb(r, g, b);
    ESP_LOGI(TAG, "UI CMD tapo preset=%d rgb=%u,%u,%u", preset, r, g, b);
    mqtt_bridge_tapo_rgb(r, g, b);
}

static void overhead_preset_cb(lv_event_t *e)
{
    discard_pending_ui(UI_PENDING_OVERHEAD);
    uintptr_t pct = (uintptr_t)lv_event_get_user_data(e);
    if (s_overhead_slider) {
        s_syncing = true;
        lv_slider_set_value(s_overhead_slider, (int32_t)pct, LV_ANIM_OFF);
        set_percent_label(s_overhead_pct, (int)pct);
        s_syncing = false;
    }
    mqtt_bridge_overhead_brightness((uint8_t)pct);
}

static void hsv_to_rgb(uint16_t hue, uint8_t sat, uint8_t val, uint8_t *r, uint8_t *g, uint8_t *b)
{
    float h = (float)(hue % 360) / 60.0f;
    float s = sat / 100.0f;
    float v = val / 100.0f;

    int i = (int)floorf(h);
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));

    float rf = 0, gf = 0, bf = 0;
    switch (i % 6) {
    case 0: rf = v; gf = t; bf = p; break;
    case 1: rf = q; gf = v; bf = p; break;
    case 2: rf = p; gf = v; bf = t; break;
    case 3: rf = p; gf = q; bf = v; break;
    case 4: rf = t; gf = p; bf = v; break;
    default: rf = v; gf = p; bf = q; break;
    }

    *r = (uint8_t)lroundf(rf * 255.0f);
    *g = (uint8_t)lroundf(gf * 255.0f);
    *b = (uint8_t)lroundf(bf * 255.0f);
}

static void colorwheel_position_knob(uint16_t hue, uint8_t sat)
{
    if (!s_color_knob) return;

    const float angle = ((float)hue) * ((float)M_PI / 180.0f);
    const float radius = 49.0f * ((float)sat / 100.0f);
    int x = 17 + 52 + (int)lroundf(cosf(angle) * radius) - 7;
    int y = 70 + 52 + (int)lroundf(sinf(angle) * radius) - 7;
    lv_obj_set_pos(s_color_knob, x, y);
}

static lv_obj_t *create_colorwheel(lv_obj_t *parent)
{
    const int w = 104;
    const int h = 104;
    const int cx = w / 2;
    const int cy = h / 2;
    const float radius = 49.0f;

    s_colorwheel_buf = heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_colorwheel_buf) {
        ESP_LOGE(TAG, "Failed to allocate color wheel canvas");
        return NULL;
    }

    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_obj_set_pos(canvas, 17, 70);
    lv_canvas_set_buffer(canvas, s_colorwheel_buf, w, h, LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(canvas, lv_color_hex(T->card_bg), LV_OPA_TRANSP);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);

    lv_display_t *disp = lv_obj_get_display(canvas);
    lv_display_enable_invalidation(disp, false);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float dx = (float)(x - cx);
            float dy = (float)(y - cy);
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist <= radius) {
                float deg = atan2f(dy, dx) * 180.0f / (float)M_PI;
                if (deg < 0) deg += 360.0f;
                uint8_t sat = (uint8_t)lroundf((dist / radius) * 100.0f);
                lv_canvas_set_px(canvas, x, y, lv_color_hsv_to_rgb((uint16_t)deg, sat, 100), LV_OPA_COVER);
            } else {
                lv_canvas_set_px(canvas, x, y, lv_color_hex(T->card_bg), LV_OPA_TRANSP);
            }
        }
    }
    lv_display_enable_invalidation(disp, true);
    lv_obj_invalidate(canvas);

    s_color_knob = lv_obj_create(parent);
    lv_obj_set_size(s_color_knob, 14, 14);
    lv_obj_set_style_radius(s_color_knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_color_knob, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_color_knob, 3, 0);
    lv_obj_set_style_border_color(s_color_knob, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(s_color_knob, 5, 0);
    lv_obj_set_style_shadow_color(s_color_knob, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(s_color_knob, 100, 0);
    lv_obj_set_style_pad_all(s_color_knob, 0, 0);
    lv_obj_remove_flag(s_color_knob, LV_OBJ_FLAG_CLICKABLE);
    colorwheel_position_knob(210, 80);
    if (s_artwork_mode) {
        lv_obj_set_style_opa(canvas, LV_OPA_TRANSP, 0);
    }

    return canvas;
}

static void colorwheel_cb(lv_event_t *e)
{
    if (s_syncing) return;
    discard_pending_ui(UI_PENDING_TAPO);

    lv_obj_t *canvas = lv_event_get_target(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;

    lv_point_t p;
    lv_area_t a;
    lv_indev_get_point(indev, &p);
    lv_obj_get_coords(canvas, &a);

    float dx = (float)(p.x - a.x1 - 52);
    float dy = (float)(p.y - a.y1 - 52);
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist > 49.0f) return;

    float deg = atan2f(dy, dx) * 180.0f / (float)M_PI;
    if (deg < 0) deg += 360.0f;
    uint8_t sat = (uint8_t)lroundf((dist / 49.0f) * 100.0f);
    uint8_t rr, gg, bb;
    hsv_to_rgb((uint16_t)deg, sat, 100, &rr, &gg, &bb);

    colorwheel_position_knob((uint16_t)deg, sat);
    color_preset_set_selected(color_preset_match(rr, gg, bb));
    mqtt_bridge_tapo_rgb(rr, gg, bb);
}

static void echo_mute_cb(lv_event_t *e)
{
    (void)e;
    discard_pending_ui(UI_PENDING_ALEXA);

    bool muted = !s_echo_muted;
    int current = s_echo_slider ? lv_slider_get_value(s_echo_slider) : 0;
    if (muted && current > 0) {
        s_echo_unmuted_volume = (uint8_t)current;
    }

    uint8_t target_volume = muted ? 0 : s_echo_unmuted_volume;
    s_echo_muted = muted;

    if (s_echo_slider) {
        s_syncing = true;
        lv_slider_set_value(s_echo_slider, target_volume, LV_ANIM_OFF);
        set_percent_label(s_echo_pct, target_volume);
        s_syncing = false;
    }
    if (s_echo_mute) {
        lv_obj_set_style_bg_color(
            s_echo_mute, c(muted ? T->accent : T->card_bg_2), 0);
    }

    ESP_LOGI(TAG, "UI CMD alexa mute=%d target_volume=%u",
             muted, target_volume);
    mqtt_bridge_echo_mute(muted, target_volume);
}

static void echo_play_cb(lv_event_t *e)
{
    (void)e;
    discard_pending_ui(UI_PENDING_ALEXA);

    bool playing = !s_echo_playing;
    s_echo_playing = playing;
    if (s_echo_play) {
        lv_obj_set_style_bg_color(
            s_echo_play, c(playing ? T->accent : T->card_bg_2), 0);
    }

    ESP_LOGI(TAG, "UI CMD alexa playing=%d", playing);
    mqtt_bridge_echo_play_pause(playing);
}

static void echo_announce_cb(lv_event_t *e)
{
    (void)e;
    discard_pending_ui(UI_PENDING_ALEXA);
    ESP_LOGI(TAG, "UI CMD alexa announce");
    mqtt_bridge_echo_announce();
}

static void make_status_dot(lv_obj_t *card, lv_obj_t **status, int x, int y)
{
    add_circle(card, x, y + 3, 9, T->online, LV_OPA_COVER);
    if (status) {
        *status = NULL;
    }
}

static void create_tapo_card(lv_obj_t *root)
{
    lv_obj_t *card = make_card(root, 14, 78, 382, 187);
    if (T->magic) {
        add_chain_icon(card, 42, 20, T->accent_2);
        make_label(card, "Tapo LED Strips", 96, 15, &lv_font_montserrat_22, T->text);
        make_status_dot(card, &s_tapo_status, 100, 48);
    } else {
        make_label(card, "Tapo LED Strips", 70, 15, &lv_font_montserrat_22, T->text);
        add_chain_icon(card, 20, 20, T->accent_2);
        make_status_dot(card, &s_tapo_status, 72, 48);
    }

    s_tapo_switch = make_switch(card, 309, 13);
    lv_obj_add_event_cb(s_tapo_switch, switch_tapo_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_colorwheel = create_colorwheel(card);
    if (s_colorwheel) {
        lv_obj_add_event_cb(s_colorwheel, colorwheel_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(s_colorwheel, colorwheel_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(s_colorwheel, colorwheel_cb, LV_EVENT_RELEASED, NULL);
    }

    for (int i = 0; i < 6; ++i) {
        lv_obj_t *btn = lv_button_create(card);
        s_color_presets[i] = btn;
        lv_obj_set_pos(btn, 145 + i * 36, 75);
        lv_obj_set_size(btn, 28, 28);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn, c(s_color_preset_display[i]), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 3, 0);
        lv_obj_set_style_shadow_opa(btn, 55, 0);
        lv_obj_add_event_cb(btn, preset_color_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
    }

    make_label(card, "Brightness", 145, 113, &lv_font_montserrat_16, T->muted);
    make_label(card, "*", 145, 140, &lv_font_montserrat_18, T->accent);

    s_tapo_slider = make_slider(card, 174, 143, 150, T->accent);
    lv_obj_add_event_cb(s_tapo_slider, tapo_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_tapo_slider, tapo_brightness_cb, LV_EVENT_RELEASED, NULL);

    s_tapo_pct = make_label(card, "--%", 332, 139, &lv_font_montserrat_16, T->text);

    if (T->kittens) {
        add_paw(card, 330, 148, T->accent, 72);
        lv_obj_t *heart = make_label(card, "<3", 8, 154,
                                     &lv_font_montserrat_14, T->accent);
        lv_obj_set_style_text_opa(heart, 120, 0);
    } else if (T->magic) {
        make_label(card, "*", 12, 48, &lv_font_montserrat_18, T->accent_2);
        make_label(card, "*", 350, 95, &lv_font_montserrat_18, T->accent_2);
        add_decor_box(card, 6, 159, 18, 18, 0x173D79, 180, 2);
        add_decor_box(card, 12, 153, 2, 30, T->accent_2, 150, 0);
        add_decor_box(card, 0, 167, 30, 2, T->accent_2, 150, 0);
    }
}

static void create_overhead_card(lv_obj_t *root)
{
    lv_obj_t *card = make_card(root, 404, 78, 382, 187);
    if (T->magic) {
        add_theme_sprite(card, THEME_ART_MAGIC_LANTERN, 20, 12, true);
        make_label(card, "Overhead Light", 88, 15, &lv_font_montserrat_22, T->text);
        make_status_dot(card, &s_overhead_status, 90, 48);
    } else {
        make_label(card, "Overhead Light", 70, 15, &lv_font_montserrat_22, T->text);
        add_bulb_icon(card, 23, 13, T->warm);
        make_status_dot(card, &s_overhead_status, 72, 48);
    }

    s_overhead_switch = make_switch(card, 309, 13);
    lv_obj_add_event_cb(s_overhead_switch, switch_overhead_cb, LV_EVENT_VALUE_CHANGED, NULL);

    make_label(card, "Brightness", 20, 78, &lv_font_montserrat_16, T->muted);
    make_label(card, "*", 20, 107, &lv_font_montserrat_18, T->warm);

    s_overhead_slider = make_slider(card, 50, 110, 250, T->warm);
    lv_obj_add_event_cb(s_overhead_slider, overhead_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_overhead_slider, overhead_brightness_cb, LV_EVENT_RELEASED, NULL);
    s_overhead_pct = make_label(card, "--%", 314, 106, &lv_font_montserrat_16, T->text);

    const int pcts[4] = {25, 50, 75, 100};
    for (int i = 0; i < 4; ++i) {
        char txt[8];
        snprintf(txt, sizeof(txt), "%d%%", pcts[i]);
        lv_obj_t *btn = make_button(card, txt, 18 + i * 88, 140, 78, 38);
        lv_obj_add_event_cb(btn, overhead_preset_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)pcts[i]);
    }

    if (T->soccer) {
        add_theme_sprite(card, THEME_ART_SOCCER_GOAL, 268, 74, true);
    } else if (T->kittens) {
        lv_obj_t *caption = make_label(card, "A Brighter\nHappier Home <3", 248, 62,
                                       &lv_font_montserrat_14, T->accent_2);
        lv_obj_set_style_text_opa(caption, 185, 0);
    }
}

static lv_obj_t *create_fan_graphic(lv_obj_t *card, int x, int y)
{
    /* Fixed outer bezel. The rotor itself is a separate object so animation
     * rotates only the blades, not the ring. */
    lv_obj_t *ring = lv_obj_create(card);
    lv_obj_set_pos(ring, x, y);
    lv_obj_set_size(ring, 112, 112);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 4, 0);
    lv_obj_set_style_border_color(ring, c(T->magic ? T->accent_2 : T->accent), 0);
    lv_obj_set_style_border_opa(ring, 195, 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *rotor = lv_obj_create(card);
    lv_obj_set_pos(rotor, x + 13, y + 13);
    lv_obj_set_size(rotor, 86, 86);
    lv_obj_set_style_bg_opa(rotor, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rotor, 0, 0);
    lv_obj_set_style_pad_all(rotor, 0, 0);
    lv_obj_remove_flag(rotor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(rotor, LV_OBJ_FLAG_CLICKABLE);

    /* Four broad rounded blades, matching the original mockup proportions. */
    const int bx[4] = {34, 50, 34, 6};
    const int by[4] = {5, 34, 50, 34};
    const int bw[4] = {18, 30, 18, 30};
    const int bh[4] = {30, 18, 30, 18};

    for (int i = 0; i < 4; ++i) {
        lv_obj_t *blade = lv_obj_create(rotor);
        lv_obj_set_pos(blade, bx[i], by[i]);
        lv_obj_set_size(blade, bw[i], bh[i]);
        lv_obj_set_style_radius(blade, 12, 0);
        lv_obj_set_style_bg_color(blade, c(T->accent), 0);
        lv_obj_set_style_bg_grad_color(blade, c(T->accent_2), 0);
        lv_obj_set_style_bg_grad_dir(blade, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(blade, 235, 0);
        lv_obj_set_style_border_width(blade, 0, 0);
        lv_obj_set_style_pad_all(blade, 0, 0);
        lv_obj_remove_flag(blade, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(blade, LV_OBJ_FLAG_CLICKABLE);
    }

    add_circle(rotor, 34, 34, 18, T->accent_2, LV_OPA_COVER);
    return rotor;
}

static void create_fan_card(lv_obj_t *root)
{
    lv_obj_t *card = make_card(root, 14, 274, 382, 192);

    if (T->kittens) {
        add_theme_sprite(card, THEME_ART_KITTEN_FAN, 40, 64, true);
        add_theme_sprite(card, THEME_ART_KITTEN_SLEEP, 246, 54, true);
    } else if (T->soccer) {
        add_theme_sprite(card, THEME_ART_SOCCER_FAN, 40, 64, true);
    } else if (T->magic) {
        add_theme_sprite(card, THEME_ART_MAGIC_FAN, 92, 54, true);
    }

    if (T->magic) {
        add_mini_fan_icon(card, 42, 14, T->accent);
        make_label(card, "Ceiling Fan", 96, 15, &lv_font_montserrat_22, T->text);
        make_status_dot(card, &s_fan_status, 100, 48);
    } else {
        make_label(card, "Ceiling Fan", 70, 15, &lv_font_montserrat_22, T->text);
        add_mini_fan_icon(card, 20, 14, T->accent);
        make_status_dot(card, &s_fan_status, 72, 48);
    }

    if (!T->kittens && !T->soccer && !T->magic) {
        s_fan_icon = create_fan_graphic(card, 25, 65);
    } else {
        s_fan_icon = NULL;
    }

    s_fan_switch = make_switch(card, 309, 13);
    lv_obj_add_event_cb(s_fan_switch, switch_fan_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_fan_state_label = make_label(card, "Fan is Off", 248, 116,
                                   &lv_font_montserrat_18, T->text);
    make_label(card, "Smart Switch", 248, 143,
               &lv_font_montserrat_14, T->muted);
}

static void create_echo_card(lv_obj_t *root)
{
    lv_obj_t *card = make_card(root, 404, 274, 382, 192);

    const int title_x = T->magic ? 112 : 70;
    const int status_x = T->magic ? 114 : 72;

    if (T->magic) {
        add_theme_sprite(card, THEME_ART_MAGIC_OWL, 30, 10, true);
        add_theme_sprite(card, THEME_ART_MAGIC_QUILL, 344, 72, true);
    }

    make_label(card, "Alexa", title_x, 15, &lv_font_montserrat_22, T->text);
    if (!T->magic) {
        make_label(card, LV_SYMBOL_VOLUME_MAX, 22, 18,
                   &lv_font_montserrat_22, T->accent);
    }
    make_status_dot(card, &s_echo_status, status_x, 48);

    const int media_x = T->magic ? 132 : 96;
    const int media_w = T->magic ? 195 : 255;

    s_alexa_now_playing = make_label(card, "Nothing playing", media_x, 45,
                                     &lv_font_montserrat_14, T->text);
    lv_obj_set_width(s_alexa_now_playing, media_w);
    lv_label_set_long_mode(s_alexa_now_playing, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    s_alexa_artist = make_label(card, "", media_x, 64,
                                &lv_font_montserrat_14, T->muted);
    lv_obj_set_width(s_alexa_artist, media_w);
    lv_label_set_long_mode(s_alexa_artist, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    make_label(card, "Volume", 20, 92, &lv_font_montserrat_16, T->muted);
    make_label(card, LV_SYMBOL_VOLUME_MAX, 20, 119,
               &lv_font_montserrat_18, T->accent);

    s_echo_slider = make_slider(card, 50, 122, 250, T->accent);
    lv_obj_add_event_cb(s_echo_slider, echo_volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_echo_slider, echo_volume_cb, LV_EVENT_RELEASED, NULL);
    s_echo_pct = make_label(card, "--%", 314, 118, &lv_font_montserrat_16, T->text);

    s_echo_mute = make_button(card, LV_SYMBOL_MUTE, 18, 148, 94, 36);
    lv_obj_add_event_cb(s_echo_mute, echo_mute_cb, LV_EVENT_CLICKED, NULL);

    s_echo_play = make_button(card, LV_SYMBOL_PLAY " " LV_SYMBOL_PAUSE,
                              126, 148, 96, 36);
    lv_obj_add_event_cb(s_echo_play, echo_play_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *announce = make_button(card, "Announce", 236, 148, 128, 36);
    lv_obj_add_event_cb(announce, echo_announce_cb, LV_EVENT_CLICKED, NULL);
}

static void create_header(lv_obj_t *root)
{
    const panel_settings_t *settings = settings_get();
    const char *title =
        (settings && settings->display_title[0]) ? settings->display_title : "Home Control";

    theme_art_id_t header_art = THEME_ART_HEADER_CLASSIC;
    if (T->soccer) {
        header_art = THEME_ART_HEADER_SOCCER;
    } else if (T->kittens) {
        header_art = THEME_ART_HEADER_KITTENS;
    } else if (T->magic) {
        header_art = THEME_ART_HEADER_MAGIC;
    }

    add_theme_sprite(root, header_art, 10, 8, false);

    lv_obj_t *title_label =
        make_label(root, title, 78, 17, &lv_font_montserrat_36, T->text);
    lv_obj_set_width(title_label, T->magic ? 500 : 530);
    lv_obj_set_height(title_label, 44);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_MODE_DOTS);

    const int clock_x = T->magic ? 650 : 635;
    const int date_x = T->magic ? 642 : 620;
    const int wifi_x = T->magic ? 752 : 733;
    const int mqtt_x = T->magic ? 738 : 720;

    s_clock = make_label(root, "--:--", clock_x, 8, &lv_font_montserrat_28, T->text);
    s_date = make_label(root, "---, --- --", date_x, 42, &lv_font_montserrat_14, T->muted);

    s_wifi_icon = make_label(root, LV_SYMBOL_WIFI, wifi_x, 20, &lv_font_montserrat_22, T->muted);

    s_mqtt_dot = lv_obj_create(root);
    lv_obj_set_pos(s_mqtt_dot, mqtt_x, 53);
    lv_obj_set_size(s_mqtt_dot, 8, 8);
    lv_obj_set_style_radius(s_mqtt_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_mqtt_dot, c(T->muted), 0);
    lv_obj_set_style_bg_opa(s_mqtt_dot, s_artwork_mode ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_mqtt_dot, 0, 0);
}

static void pending_state_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    pending_ui_state_t pending = {0};

    portENTER_CRITICAL(&s_pending_ui_mux);
    if (s_pending_ui.dirty == 0) {
        portEXIT_CRITICAL(&s_pending_ui_mux);
        return;
    }

    pending = s_pending_ui;
    s_pending_ui.dirty = 0;
    portEXIT_CRITICAL(&s_pending_ui_mux);

    if ((pending.dirty & UI_PENDING_NETWORK) && s_wifi_icon) {
        lv_obj_set_style_text_color(
            s_wifi_icon,
            c(pending.network_online ? T->online : T->muted),
            0);
    }

    if ((pending.dirty & UI_PENDING_MQTT) && s_mqtt_dot) {
        lv_obj_set_style_bg_color(
            s_mqtt_dot,
            c(pending.mqtt_online ? T->online : T->muted),
            0);
    }

    if ((pending.dirty & UI_PENDING_TAPO) &&
        s_tapo_switch && s_tapo_slider && s_tapo_pct) {
        s_syncing = true;
        set_switch(s_tapo_switch, pending.tapo_on);
        lv_slider_set_value(
            s_tapo_slider, pending.tapo_brightness, LV_ANIM_OFF);
        set_percent_label(s_tapo_pct, pending.tapo_brightness);
        color_ui_apply_rgb(
            pending.tapo_r, pending.tapo_g, pending.tapo_b);
        s_syncing = false;
    }

    if ((pending.dirty & UI_PENDING_OVERHEAD) &&
        s_overhead_switch && s_overhead_slider && s_overhead_pct) {
        s_syncing = true;
        set_switch(s_overhead_switch, pending.overhead_on);
        lv_slider_set_value(
            s_overhead_slider, pending.overhead_brightness, LV_ANIM_OFF);
        set_percent_label(s_overhead_pct, pending.overhead_brightness);
        s_syncing = false;
    }

    if ((pending.dirty & UI_PENDING_FAN) &&
        s_fan_switch && s_fan_state_label) {
        s_syncing = true;
        s_fan_on = pending.fan_on;
        set_switch(s_fan_switch, pending.fan_on);
        lv_label_set_text(
            s_fan_state_label,
            pending.fan_on ? "Fan is On" : "Fan is Off");
        if (s_fan_icon) {
            lv_obj_set_style_shadow_opa(
                s_fan_icon, pending.fan_on ? 100 : 20, 0);
        }
        s_syncing = false;
    }

    if ((pending.dirty & UI_PENDING_ALEXA) &&
        s_echo_slider && s_echo_pct && s_echo_mute) {
        s_syncing = true;
        s_echo_muted = pending.alexa_muted;
        s_echo_playing = pending.alexa_playing;
        if (pending.alexa_volume > 0) {
            s_echo_unmuted_volume = pending.alexa_volume;
        }
        lv_slider_set_value(
            s_echo_slider, pending.alexa_volume, LV_ANIM_OFF);
        set_percent_label(s_echo_pct, pending.alexa_volume);
        lv_obj_set_style_bg_color(
            s_echo_mute,
            c(pending.alexa_muted ? T->accent : T->card_bg_2),
            0);
        if (s_echo_play) {
            lv_obj_set_style_bg_color(
                s_echo_play,
                c(pending.alexa_playing ? T->accent : T->card_bg_2),
                0);
        }

        if (s_alexa_now_playing) {
            lv_label_set_text(
                s_alexa_now_playing,
                pending.alexa_title[0] ? pending.alexa_title : "Nothing playing");
        }
        if (s_alexa_artist) {
            lv_label_set_text(s_alexa_artist, pending.alexa_artist);
        }

        s_syncing = false;
    }

    ESP_LOGI(TAG, "UI APPLY pending state mask=0x%02lx",
             (unsigned long)pending.dirty);
}

static void clock_timer_cb(lv_timer_t *timer)
{
    time_t now;
    struct tm tm_now;
    char time_buf[16];
    char date_buf[24];

    time(&now);
    localtime_r(&now, &tm_now);

    if (tm_now.tm_year < (2020 - 1900)) {
        return;
    }

    strftime(time_buf, sizeof(time_buf), "%I:%M", &tm_now);
    if (time_buf[0] == '0') {
        memmove(time_buf, time_buf + 1, strlen(time_buf));
    }
    strftime(date_buf, sizeof(date_buf), "%a, %b %d", &tm_now);

    lv_label_set_text(s_clock, time_buf);
    lv_label_set_text(s_date, date_buf);
}

static void animation_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (s_fan_on && s_fan_icon) {
        s_fan_angle += 80;
        if (s_fan_angle >= 3600) s_fan_angle -= 3600;
        lv_obj_set_style_transform_rotation(s_fan_icon, s_fan_angle, 0);
    }
}

esp_err_t ui_start(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t touch)
{
    T = panel_theme_get();
    ESP_LOGI(TAG, "Starting LVGL theme id=%d title=\"%s\"",
             (int)T->id,
             settings_get()->display_title[0] ? settings_get()->display_title : "Home Control");

    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 12288,
        .task_affinity = -1,
        .task_max_sleep_ms = 100,
        .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port init failed");

#ifdef WOKWI_EMULATOR
    (void)panel;
    (void)touch;
    s_disp = wokwi_panel_create_display();
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "Failed to create Wokwi display");
    ESP_RETURN_ON_ERROR(wokwi_panel_add_touch(s_disp), TAG, "Failed to create Wokwi touch input");
#else
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel,
        .buffer_size = PANEL_H_RES * PANEL_V_RES,
        .double_buffer = true,
        .hres = PANEL_H_RES,
        .vres = PANEL_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .full_refresh = true,
            .direct_mode = false,
            .swap_bytes = false,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };

    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "Failed to add RGB display to LVGL");
    ESP_LOGI(TAG, "RGB display: two PSRAM framebuffers, full refresh, bounce-buffer scanout, tear avoidance");

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_disp,
        .handle = touch,
    };
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg), ESP_FAIL, TAG, "Failed to add touch input");
#endif

    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }

    lv_obj_t *root = lv_screen_active();
    style_screen(root);

    /*
     * The previous full-screen raster assets were complete UI mockups rather
     * than background-only art. Drawing live LVGL controls over those images
     * duplicated switches, sliders, labels, and status graphics. Render the
     * entire interface natively so every visible control has one owner.
     */
    s_artwork_mode = false;
    init_styles();
    add_theme_decor(root);
    create_header(root);
    create_tapo_card(root);
    create_overhead_card(root);
    create_fan_card(root);
    create_echo_card(root);

    lv_timer_create(pending_state_timer_cb, 20, NULL);
    lv_timer_create(clock_timer_cb, 1000, NULL);
    lv_timer_create(animation_timer_cb, 70, NULL);
    clock_timer_cb(NULL);

    lvgl_port_unlock();

    return ESP_OK;
}

void ui_set_network_online(bool online)
{
    portENTER_CRITICAL(&s_pending_ui_mux);
    s_pending_ui.network_online = online;
    s_pending_ui.dirty |= UI_PENDING_NETWORK;
    portEXIT_CRITICAL(&s_pending_ui_mux);
}

void ui_set_mqtt_online(bool online)
{
    portENTER_CRITICAL(&s_pending_ui_mux);
    s_pending_ui.mqtt_online = online;
    s_pending_ui.dirty |= UI_PENDING_MQTT;
    portEXIT_CRITICAL(&s_pending_ui_mux);
}

void ui_apply_tapo_state(bool on, uint8_t brightness, uint8_t r, uint8_t g, uint8_t b)
{
    portENTER_CRITICAL(&s_pending_ui_mux);
    s_pending_ui.tapo_on = on;
    s_pending_ui.tapo_brightness = brightness;
    s_pending_ui.tapo_r = r;
    s_pending_ui.tapo_g = g;
    s_pending_ui.tapo_b = b;
    s_pending_ui.dirty |= UI_PENDING_TAPO;
    portEXIT_CRITICAL(&s_pending_ui_mux);
}

void ui_apply_overhead_state(bool on, uint8_t brightness)
{
    portENTER_CRITICAL(&s_pending_ui_mux);
    s_pending_ui.overhead_on = on;
    s_pending_ui.overhead_brightness = brightness;
    s_pending_ui.dirty |= UI_PENDING_OVERHEAD;
    portEXIT_CRITICAL(&s_pending_ui_mux);
}

void ui_apply_fan_state(bool on)
{
    portENTER_CRITICAL(&s_pending_ui_mux);
    s_pending_ui.fan_on = on;
    s_pending_ui.dirty |= UI_PENDING_FAN;
    portEXIT_CRITICAL(&s_pending_ui_mux);
}

void ui_apply_echo_state(uint8_t volume, bool muted, bool playing,
                         const char *title, const char *artist)
{
    portENTER_CRITICAL(&s_pending_ui_mux);
    s_pending_ui.alexa_volume = volume;
    s_pending_ui.alexa_muted = muted;
    s_pending_ui.alexa_playing = playing;
    strlcpy(s_pending_ui.alexa_title, title ? title : "",
            sizeof(s_pending_ui.alexa_title));
    strlcpy(s_pending_ui.alexa_artist, artist ? artist : "",
            sizeof(s_pending_ui.alexa_artist));
    s_pending_ui.dirty |= UI_PENDING_ALEXA;
    portEXIT_CRITICAL(&s_pending_ui_mux);
}
