#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "board_7c.h"
#include "artwork.h"
#include "lvgl.h"
#include "mqtt_bridge.h"
#include "ui_theme.h"
#ifdef WOKWI_EMULATOR
#include "wokwi_panel.h"
#endif

static const char *TAG = "ui";

static const panel_theme_t *T;

static lv_display_t *s_disp;
static bool s_artwork_mode = true;
static lv_image_dsc_t s_artwork_dsc;
static lv_obj_t *s_artwork_image;

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
static void *s_colorwheel_buf;

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
static lv_obj_t *s_wave[14];

static bool s_syncing;
static bool s_fan_on;
static bool s_echo_playing;
static bool s_echo_muted;
static int s_fan_angle;
static int s_wave_phase;

static lv_style_t s_card_style;
static lv_style_t s_button_style;
static lv_style_t s_button_pressed_style;

static inline lv_color_t c(uint32_t hex)
{
    return lv_color_hex(hex);
}

static void create_artwork_background(lv_obj_t *root)
{
    artwork_svg_t art = artwork_get_active_svg();
    if (!art.data || art.size == 0) {
        ESP_LOGW(TAG, "No artwork data available; using native LVGL fallback");
        s_artwork_mode = false;
        return;
    }

    /*
     * The current artwork assets are SVG containers around base64-encoded
     * raster images (WebP). LVGL 9.5's bundled ThorVG build can parse SVG, but
     * it does not include raster image loaders such as WebP. Attempting to use
     * those assets therefore produces an empty image while artwork mode hides
     * the native controls, leaving the panel apparently blank.
     *
     * Keep support for future pure-vector SVG assets, but fail visibly to the
     * native LVGL theme for raster-backed SVGs.
     */
    const char *svg_text = (const char *)art.data;
    if (strstr(svg_text, "data:image/") != NULL) {
        ESP_LOGW(TAG, "Artwork contains embedded raster images unsupported by bundled ThorVG; using native LVGL fallback");
        s_artwork_mode = false;
        return;
    }

    memset(&s_artwork_dsc, 0, sizeof(s_artwork_dsc));
    s_artwork_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_artwork_dsc.header.w = PANEL_H_RES;
    s_artwork_dsc.header.h = PANEL_V_RES;
    s_artwork_dsc.data_size = art.size;
    s_artwork_dsc.data = art.data;

    lv_image_header_t header;
    if (lv_image_decoder_get_info(&s_artwork_dsc, &header) != LV_RESULT_OK) {
        ESP_LOGW(TAG, "SVG artwork decoder rejected asset; using native LVGL fallback");
        s_artwork_mode = false;
        return;
    }

    s_artwork_image = lv_image_create(root);
    lv_image_set_src(s_artwork_image, &s_artwork_dsc);
    lv_obj_set_pos(s_artwork_image, 0, 0);
    lv_obj_set_size(s_artwork_image, PANEL_H_RES, PANEL_V_RES);
    lv_obj_remove_flag(s_artwork_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_artwork_image, LV_OBJ_FLAG_SCROLLABLE);
    ESP_LOGI(TAG, "Loaded %u-byte pure-vector SVG artwork background", (unsigned)art.size);
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
    lv_style_set_radius(&s_card_style, 16);
    lv_style_set_bg_color(&s_card_style, c(T->card_bg));
    lv_style_set_bg_grad_color(&s_card_style, c(T->card_bg_2));
    lv_style_set_bg_grad_dir(&s_card_style, LV_GRAD_DIR_VER);
    lv_style_set_bg_opa(&s_card_style, s_artwork_mode ? LV_OPA_TRANSP : 242);
    lv_style_set_border_width(&s_card_style, T->magic ? 2 : 1);
    lv_style_set_border_color(&s_card_style, c(T->card_border));
    lv_style_set_border_opa(&s_card_style, s_artwork_mode ? LV_OPA_TRANSP : (T->magic ? 220 : 150));
    lv_style_set_shadow_width(&s_card_style, 14);
    lv_style_set_shadow_spread(&s_card_style, 1);
    lv_style_set_shadow_color(&s_card_style, c(T->accent));
    lv_style_set_shadow_opa(&s_card_style, s_artwork_mode ? LV_OPA_TRANSP : (T->kittens ? 45 : 30));
    lv_style_set_pad_all(&s_card_style, 0);

    lv_style_init(&s_button_style);
    lv_style_set_radius(&s_button_style, 11);
    lv_style_set_bg_color(&s_button_style, c(T->card_bg_2));
    lv_style_set_bg_opa(&s_button_style, s_artwork_mode ? LV_OPA_TRANSP : 220);
    lv_style_set_border_width(&s_button_style, 1);
    lv_style_set_border_color(&s_button_style, c(T->card_border));
    lv_style_set_border_opa(&s_button_style, s_artwork_mode ? LV_OPA_TRANSP : 160);
    lv_style_set_text_color(&s_button_style, c(T->text));
    lv_style_set_shadow_width(&s_button_style, 5);
    lv_style_set_shadow_color(&s_button_style, c(T->accent));
    lv_style_set_shadow_opa(&s_button_style, s_artwork_mode ? LV_OPA_TRANSP : 30);

    lv_style_init(&s_button_pressed_style);
    lv_style_set_bg_color(&s_button_pressed_style, c(T->accent));
    lv_style_set_bg_opa(&s_button_pressed_style, 180);
    lv_style_set_transform_width(&s_button_pressed_style, -2);
    lv_style_set_transform_height(&s_button_pressed_style, -2);
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
    return card;
}

static lv_obj_t *make_switch(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_pos(sw, x, y);
    lv_obj_set_size(sw, 58, 32);
    lv_obj_set_style_bg_color(sw, c(0x33475B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, c(T->accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, c(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_shadow_width(sw, 7, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(sw, 80, LV_PART_KNOB);
    return sw;
}

static lv_obj_t *make_slider(lv_obj_t *parent, int x, int y, int w, uint32_t accent)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, w, 18);
    lv_slider_set_range(slider, 0, 100);

    lv_obj_set_style_bg_color(slider, c(0x3B4A58), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, 180, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 10, LV_PART_MAIN);

    lv_obj_set_style_bg_color(slider, c(accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 10, LV_PART_INDICATOR);

    lv_obj_set_style_bg_color(slider, c(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 7, LV_PART_KNOB);
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
}

static void add_soccer_decor(lv_obj_t *screen)
{
    lv_obj_t *pitch = lv_obj_create(screen);
    lv_obj_set_pos(pitch, 0, 72);
    lv_obj_set_size(pitch, 800, 408);
    lv_obj_set_style_bg_opa(pitch, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pitch, 1, 0);
    lv_obj_set_style_border_color(pitch, c(0x2FA358), 0);
    lv_obj_set_style_border_opa(pitch, 38, 0);
    lv_obj_set_style_radius(pitch, 0, 0);
    lv_obj_set_style_pad_all(pitch, 0, 0);
    lv_obj_remove_flag(pitch, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mid = lv_obj_create(pitch);
    lv_obj_set_pos(mid, 399, 0);
    lv_obj_set_size(mid, 2, 408);
    lv_obj_set_style_bg_color(mid, c(0x50C878), 0);
    lv_obj_set_style_bg_opa(mid, 28, 0);
    lv_obj_set_style_border_width(mid, 0, 0);

    lv_obj_t *center = lv_obj_create(pitch);
    lv_obj_set_pos(center, 330, 130);
    lv_obj_set_size(center, 140, 140);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center, 2, 0);
    lv_obj_set_style_border_color(center, c(0x50C878), 0);
    lv_obj_set_style_border_opa(center, 28, 0);

    add_circle(screen, 735, 6, 52, 0x0C3F23, 220);
    add_circle(screen, 748, 19, 26, 0xE6F6EA, 70);
    add_circle(screen, 754, 25, 14, 0x082A18, 180);
}

static void add_paw(lv_obj_t *parent, int x, int y, uint32_t color, lv_opa_t opa)
{
    add_circle(parent, x + 8, y + 12, 10, color, opa);
    add_circle(parent, x + 2, y + 2, 6, color, opa);
    add_circle(parent, x + 10, y, 6, color, opa);
    add_circle(parent, x + 18, y + 3, 6, color, opa);
}

static void add_kittens_decor(lv_obj_t *screen)
{
    add_paw(screen, 15, 18, T->accent, 180);
    add_paw(screen, 610, 25, T->accent, 90);
    add_paw(screen, 760, 52, T->accent, 140);

    lv_obj_t *cat = make_label(screen, "=^.^=", 690, 8, &lv_font_montserrat_20, T->accent_2);
    lv_obj_set_style_text_opa(cat, 220, 0);

    lv_obj_t *heart = make_label(screen, "<3", 760, 42, &lv_font_montserrat_18, T->accent);
    lv_obj_set_style_text_opa(heart, 180, 0);
}

static void add_magic_decor(lv_obj_t *screen)
{
    /* Moon */
    add_circle(screen, 625, 5, 52, 0xB7D8FF, 95);
    add_circle(screen, 638, 4, 45, T->screen_bg, 245);

    /* Simple castle skyline behind header text. */
    for (int i = 0; i < 5; ++i) {
        int x = 470 + i * 29;
        int h = 26 + (i % 2) * 15;
        lv_obj_t *tower = lv_obj_create(screen);
        lv_obj_set_pos(tower, x, 48 - h);
        lv_obj_set_size(tower, 20, h);
        lv_obj_set_style_bg_color(tower, c(0x071830), 0);
        lv_obj_set_style_bg_opa(tower, 230, 0);
        lv_obj_set_style_border_width(tower, 0, 0);
        lv_obj_set_style_radius(tower, 0, 0);
        lv_obj_set_style_pad_all(tower, 0, 0);
        make_label(tower, "*", 6, h - 20, &lv_font_montserrat_14, T->accent_2);
    }

    for (int i = 0; i < 9; ++i) {
        lv_obj_t *star = make_label(screen, "*", 410 + i * 41, 5 + (i % 3) * 13,
                                    &lv_font_montserrat_14, T->accent_2);
        lv_obj_set_style_text_opa(star, 150 + (i % 2) * 70, 0);
    }
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
    mqtt_bridge_tapo_power(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void switch_overhead_cb(lv_event_t *e)
{
    if (s_syncing) return;
    mqtt_bridge_overhead_power(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void switch_fan_cb(lv_event_t *e)
{
    if (s_syncing) return;
    mqtt_bridge_fan_power(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static void tapo_brightness_cb(lv_event_t *e)
{
    if (s_syncing) return;
    int value = lv_slider_get_value(lv_event_get_target(e));
    mqtt_bridge_tapo_brightness((uint8_t)value);
}

static void overhead_brightness_cb(lv_event_t *e)
{
    if (s_syncing) return;
    int value = lv_slider_get_value(lv_event_get_target(e));
    mqtt_bridge_overhead_brightness((uint8_t)value);
}

static void echo_volume_cb(lv_event_t *e)
{
    if (s_syncing) return;
    int value = lv_slider_get_value(lv_event_get_target(e));
    mqtt_bridge_echo_volume((uint8_t)value);
}

static void preset_color_cb(lv_event_t *e)
{
    uintptr_t rgb = (uintptr_t)lv_event_get_user_data(e);
    mqtt_bridge_tapo_rgb((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static void overhead_preset_cb(lv_event_t *e)
{
    uintptr_t pct = (uintptr_t)lv_event_get_user_data(e);
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
    const float radius = 45.0f * ((float)sat / 100.0f);
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
    mqtt_bridge_tapo_rgb(rr, gg, bb);
}

static void echo_mute_cb(lv_event_t *e)
{
    mqtt_bridge_echo_mute(!s_echo_muted);
}

static void echo_play_cb(lv_event_t *e)
{
    mqtt_bridge_echo_play_pause();
}

static void echo_announce_cb(lv_event_t *e)
{
    mqtt_bridge_echo_announce();
}

static void make_status_dot(lv_obj_t *card, lv_obj_t **status, int x, int y)
{
    add_circle(card, x, y + 3, 9, T->online, LV_OPA_COVER);
    *status = make_label(card, "Online", x + 15, y, &lv_font_montserrat_14, T->online);
}

static void create_tapo_card(lv_obj_t *root)
{
    lv_obj_t *card = make_card(root, 14, 78, 382, 187);
    make_label(card, "Tapo LED Strips", 70, 15, &lv_font_montserrat_22, T->text);
    make_label(card, "LED", 19, 14, &lv_font_montserrat_18, T->accent_2);
    make_status_dot(card, &s_tapo_status, 76, 47);

    s_tapo_switch = make_switch(card, 309, 13);
    lv_obj_add_event_cb(s_tapo_switch, switch_tapo_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_colorwheel = create_colorwheel(card);
    if (s_colorwheel) {
        lv_obj_add_event_cb(s_colorwheel, colorwheel_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(s_colorwheel, colorwheel_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(s_colorwheel, colorwheel_cb, LV_EVENT_RELEASED, NULL);
    }

    const uint32_t colors[6] = {0xFF3B30, 0xFF9500, 0xFFD60A, 0x30D158, 0x0A84FF, 0xBF5AF2};
    const uint32_t payloads[6] = {0xFF0000, 0xFF8000, 0xFFFF00, 0x00FF00, 0x0066FF, 0xA020F0};
    for (int i = 0; i < 6; ++i) {
        lv_obj_t *btn = lv_button_create(card);
        lv_obj_set_pos(btn, 145 + i * 36, 75);
        lv_obj_set_size(btn, 28, 28);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn, c(colors[i]), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 5, 0);
        lv_obj_set_style_shadow_opa(btn, 80, 0);
        lv_obj_add_event_cb(btn, preset_color_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)payloads[i]);
    }

    make_label(card, "Brightness", 145, 113, &lv_font_montserrat_16, T->muted);
    make_label(card, "*", 145, 140, &lv_font_montserrat_18, T->accent);

    s_tapo_slider = make_slider(card, 174, 143, 150, T->accent);
    lv_obj_add_event_cb(s_tapo_slider, tapo_brightness_cb, LV_EVENT_RELEASED, NULL);

    s_tapo_pct = make_label(card, "0%", 328, 139, &lv_font_montserrat_16, T->text);
}

static void create_overhead_card(lv_obj_t *root)
{
    lv_obj_t *card = make_card(root, 404, 78, 382, 187);
    make_label(card, "Overhead Light", 70, 15, &lv_font_montserrat_22, T->text);
    make_label(card, LV_SYMBOL_POWER, 25, 17, &lv_font_montserrat_24, T->warm);
    make_status_dot(card, &s_overhead_status, 76, 47);

    s_overhead_switch = make_switch(card, 309, 13);
    lv_obj_add_event_cb(s_overhead_switch, switch_overhead_cb, LV_EVENT_VALUE_CHANGED, NULL);

    make_label(card, "Brightness", 20, 78, &lv_font_montserrat_16, T->muted);
    make_label(card, "*", 20, 107, &lv_font_montserrat_18, T->warm);

    s_overhead_slider = make_slider(card, 50, 110, 250, T->warm);
    lv_obj_add_event_cb(s_overhead_slider, overhead_brightness_cb, LV_EVENT_RELEASED, NULL);

    s_overhead_pct = make_label(card, "0%", 310, 106, &lv_font_montserrat_16, T->text);

    const int pcts[4] = {25, 50, 75, 100};
    for (int i = 0; i < 4; ++i) {
        char txt[8];
        snprintf(txt, sizeof(txt), "%d%%", pcts[i]);
        lv_obj_t *btn = make_button(card, txt, 18 + i * 88, 140, 78, 38);
        lv_obj_add_event_cb(btn, overhead_preset_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)pcts[i]);
    }

    if (T->soccer) {
        lv_obj_t *goal = lv_obj_create(card);
        lv_obj_set_pos(goal, 270, 65);
        lv_obj_set_size(goal, 90, 62);
        lv_obj_set_style_bg_opa(goal, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(goal, 2, 0);
        lv_obj_set_style_border_color(goal, c(T->accent), 0);
        lv_obj_set_style_border_opa(goal, 42, 0);
        lv_obj_set_style_radius(goal, 0, 0);
    } else if (T->kittens) {
        make_label(card, "A brighter\nhappier home", 240, 70, &lv_font_montserrat_14, T->accent_2);
        add_paw(card, 330, 94, T->accent, 100);
    } else if (T->magic) {
        make_label(card, "*  *   *", 245, 69, &lv_font_montserrat_16, T->accent_2);
    }
}

static lv_obj_t *create_fan_graphic(lv_obj_t *card, int x, int y)
{
    lv_obj_t *fan = lv_obj_create(card);
    lv_obj_set_pos(fan, x, y);
    lv_obj_set_size(fan, 112, 112);
    lv_obj_set_style_radius(fan, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(fan, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fan, 4, 0);
    lv_obj_set_style_border_color(fan, c(T->accent), 0);
    lv_obj_set_style_border_opa(fan, 180, 0);
    lv_obj_set_style_pad_all(fan, 0, 0);
    lv_obj_remove_flag(fan, LV_OBJ_FLAG_SCROLLABLE);

    add_circle(fan, 47, 47, 18, T->accent, LV_OPA_COVER);

    /* Four rounded blades. */
    const int bx[4] = {44, 65, 44, 24};
    const int by[4] = {12, 43, 68, 43};
    const int bw[4] = {24, 34, 24, 34};
    const int bh[4] = {34, 24, 34, 24};
    for (int i = 0; i < 4; ++i) {
        lv_obj_t *blade = lv_obj_create(fan);
        lv_obj_set_pos(blade, bx[i], by[i]);
        lv_obj_set_size(blade, bw[i], bh[i]);
        lv_obj_set_style_radius(blade, 18, 0);
        lv_obj_set_style_bg_color(blade, c(T->accent), 0);
        lv_obj_set_style_bg_grad_color(blade, c(T->accent_2), 0);
        lv_obj_set_style_bg_grad_dir(blade, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(blade, 220, 0);
        lv_obj_set_style_border_width(blade, 0, 0);
        lv_obj_set_style_pad_all(blade, 0, 0);
    }

    lv_obj_set_style_transform_width(fan, 18, 0);
    lv_obj_set_style_transform_height(fan, 18, 0);
    if (s_artwork_mode) {
        lv_obj_set_style_opa(fan, LV_OPA_TRANSP, 0);
    }
    return fan;
}

static void create_fan_card(lv_obj_t *root)
{
    lv_obj_t *card = make_card(root, 14, 274, 382, 192);
    make_label(card, "Ceiling Fan", 70, 15, &lv_font_montserrat_22, T->text);
    make_label(card, "FAN", 19, 18, &lv_font_montserrat_16, T->accent);
    make_status_dot(card, &s_fan_status, 76, 47);

    s_fan_icon = create_fan_graphic(card, 25, 65);

    s_fan_switch = make_switch(card, 248, 72);
    lv_obj_add_event_cb(s_fan_switch, switch_fan_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_fan_state_label = make_label(card, "Fan is Off", 237, 116, &lv_font_montserrat_18, T->text);
    make_label(card, "Smart Switch", 237, 143, &lv_font_montserrat_14, T->muted);

    if (T->soccer) {
        lv_obj_t *line = lv_obj_create(card);
        lv_obj_set_pos(line, 183, 64);
        lv_obj_set_size(line, 2, 128);
        lv_obj_set_style_bg_color(line, c(T->accent), 0);
        lv_obj_set_style_bg_opa(line, 40, 0);
        lv_obj_set_style_border_width(line, 0, 0);
    } else if (T->kittens) {
        make_label(card, "=^..^=", 210, 64, &lv_font_montserrat_18, T->accent_2);
        make_label(card, "z  z  z", 215, 48, &lv_font_montserrat_14, T->accent_2);
        add_paw(card, 20, 156, T->accent, 90);
    } else if (T->magic) {
        lv_obj_set_style_border_color(s_fan_icon, c(T->accent_2), 0);
        make_label(card, "*", 165, 72, &lv_font_montserrat_20, T->accent_2);
        make_label(card, "*", 345, 145, &lv_font_montserrat_20, T->accent_2);
    }
}

static void create_echo_wave(lv_obj_t *card)
{
    for (int i = 0; i < 14; ++i) {
        lv_obj_t *bar = lv_obj_create(card);
        lv_obj_set_pos(bar, 145 + i * 10, 71);
        lv_obj_set_size(bar, 5, 10 + ((i * 7) % 26));
        lv_obj_set_style_bg_color(bar, c(T->accent), 0);
        lv_obj_set_style_bg_opa(bar, s_artwork_mode ? LV_OPA_TRANSP : 100, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        s_wave[i] = bar;
    }
}

static void create_echo_card(lv_obj_t *root)
{
    lv_obj_t *card = make_card(root, 404, 274, 382, 192);
    make_label(card, "Echo Dot", 70, 15, &lv_font_montserrat_22, T->text);
    make_label(card, LV_SYMBOL_VOLUME_MAX, 22, 18, &lv_font_montserrat_22, T->accent);
    make_status_dot(card, &s_echo_status, 76, 47);

    make_label(card, "alexa", 310, 18, &lv_font_montserrat_16, T->accent);

    create_echo_wave(card);

    make_label(card, "Volume", 20, 82, &lv_font_montserrat_16, T->muted);
    make_label(card, LV_SYMBOL_VOLUME_MAX, 20, 109, &lv_font_montserrat_18, T->accent);

    s_echo_slider = make_slider(card, 50, 112, 250, T->accent);
    lv_obj_add_event_cb(s_echo_slider, echo_volume_cb, LV_EVENT_RELEASED, NULL);
    s_echo_pct = make_label(card, "0%", 310, 108, &lv_font_montserrat_16, T->text);

    s_echo_mute = make_button(card, LV_SYMBOL_MUTE, 18, 143, 94, 40);
    lv_obj_add_event_cb(s_echo_mute, echo_mute_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *play = make_button(card, LV_SYMBOL_PLAY " " LV_SYMBOL_PAUSE, 126, 143, 96, 40);
    lv_obj_add_event_cb(play, echo_play_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *announce = make_button(card, "Announce", 236, 143, 128, 40);
    lv_obj_add_event_cb(announce, echo_announce_cb, LV_EVENT_CLICKED, NULL);

    if (T->kittens) {
        add_paw(card, 325, 66, T->accent, 100);
    } else if (T->magic) {
        make_label(card, "* * *", 275, 67, &lv_font_montserrat_14, T->accent_2);
    }
}

static void create_header(lv_obj_t *root)
{
    make_label(root, LV_SYMBOL_HOME, 20, 17, &lv_font_montserrat_28, T->accent);
    make_label(root, T->name, 64, 10, &lv_font_montserrat_32, T->text);
    make_label(root, T->tagline, 64, 48, &lv_font_montserrat_14, T->muted);

    s_clock = make_label(root, "--:--", 635, 8, &lv_font_montserrat_28, T->text);
    s_date = make_label(root, "---, --- --", 620, 42, &lv_font_montserrat_14, T->muted);

    s_wifi_icon = make_label(root, LV_SYMBOL_WIFI, 733, 20, &lv_font_montserrat_22, T->muted);
    make_label(root, LV_SYMBOL_SETTINGS, 770, 20, &lv_font_montserrat_22, T->muted);

    s_mqtt_dot = lv_obj_create(root);
    lv_obj_set_pos(s_mqtt_dot, 720, 53);
    lv_obj_set_size(s_mqtt_dot, 8, 8);
    lv_obj_set_style_radius(s_mqtt_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_mqtt_dot, c(T->muted), 0);
    lv_obj_set_style_bg_opa(s_mqtt_dot, s_artwork_mode ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_mqtt_dot, 0, 0);
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
    if (s_fan_on && s_fan_icon) {
        s_fan_angle += 80;
        if (s_fan_angle >= 3600) s_fan_angle -= 3600;
        lv_obj_set_style_transform_rotation(s_fan_icon, s_fan_angle, 0);
    }

    if (s_artwork_mode) {
        return;
    }

    if (s_echo_playing) {
        ++s_wave_phase;
        for (int i = 0; i < 14; ++i) {
            int h = 8 + ((i * 17 + s_wave_phase * 11) % 34);
            lv_obj_set_height(s_wave[i], h);
            lv_obj_set_style_bg_opa(s_wave[i], 120 + ((i * 13 + s_wave_phase * 19) % 100), 0);
        }
    } else {
        for (int i = 0; i < 14; ++i) {
            lv_obj_set_height(s_wave[i], 8);
            lv_obj_set_style_bg_opa(s_wave[i], 45, 0);
        }
    }
}

esp_err_t ui_start(esp_lcd_panel_handle_t panel, esp_lcd_touch_handle_t touch)
{
    T = panel_theme_get();
    ESP_LOGI(TAG, "Starting LVGL theme: %s", T->tagline);

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
        .double_buffer = false,
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
            .buff_spiram = false,
            .direct_mode = true,
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
     * Determine whether artwork rendering is usable before initializing
     * styles, because style opacity depends on s_artwork_mode.
     */
    create_artwork_background(root);
    init_styles();

    if (!s_artwork_mode) {
        add_theme_decor(root);
    }
    create_header(root);
    create_tapo_card(root);
    create_overhead_card(root);
    create_fan_card(root);
    create_echo_card(root);

    lv_timer_create(clock_timer_cb, 1000, NULL);
    lv_timer_create(animation_timer_cb, 70, NULL);
    clock_timer_cb(NULL);

    lvgl_port_unlock();

    return ESP_OK;
}

void ui_set_network_online(bool online)
{
    if (!s_wifi_icon || !lvgl_port_lock(50)) return;
    lv_obj_set_style_text_color(s_wifi_icon, c(online ? T->online : T->muted), 0);
    lvgl_port_unlock();
}

void ui_set_mqtt_online(bool online)
{
    if (!s_mqtt_dot || !lvgl_port_lock(50)) return;
    lv_obj_set_style_bg_color(s_mqtt_dot, c(online ? T->online : T->muted), 0);
    lvgl_port_unlock();
}

void ui_apply_tapo_state(bool on, uint8_t brightness, uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_tapo_switch || !lvgl_port_lock(100)) return;

    s_syncing = true;
    set_switch(s_tapo_switch, on);
    lv_slider_set_value(s_tapo_slider, brightness, LV_ANIM_ON);
    lv_label_set_text_fmt(s_tapo_pct, "%u%%", brightness);
    lv_label_set_text(s_tapo_status, on ? "Online • On" : "Online • Off");
    lv_color_hsv_t hsv = lv_color_rgb_to_hsv(r, g, b);
    colorwheel_position_knob(hsv.h, hsv.s);
    s_syncing = false;

    lvgl_port_unlock();
}

void ui_apply_overhead_state(bool on, uint8_t brightness)
{
    if (!s_overhead_switch || !lvgl_port_lock(100)) return;

    s_syncing = true;
    set_switch(s_overhead_switch, on);
    lv_slider_set_value(s_overhead_slider, brightness, LV_ANIM_ON);
    lv_label_set_text_fmt(s_overhead_pct, "%u%%", brightness);
    lv_label_set_text(s_overhead_status, on ? "Online • On" : "Online • Off");
    s_syncing = false;

    lvgl_port_unlock();
}

void ui_apply_fan_state(bool on)
{
    if (!s_fan_switch || !lvgl_port_lock(100)) return;

    s_syncing = true;
    s_fan_on = on;
    set_switch(s_fan_switch, on);
    lv_label_set_text(s_fan_status, on ? "Online • On" : "Online • Off");
    lv_label_set_text(s_fan_state_label, on ? "Fan is On" : "Fan is Off");
    lv_obj_set_style_shadow_opa(s_fan_icon, on ? 100 : 20, 0);
    s_syncing = false;

    lvgl_port_unlock();
}

void ui_apply_echo_state(uint8_t volume, bool muted, bool playing)
{
    if (!s_echo_slider || !lvgl_port_lock(100)) return;

    s_syncing = true;
    s_echo_muted = muted;
    s_echo_playing = playing;
    lv_slider_set_value(s_echo_slider, volume, LV_ANIM_ON);
    lv_label_set_text_fmt(s_echo_pct, "%u%%", volume);
    lv_label_set_text(s_echo_status, playing ? "Online • Playing" : "Online");
    lv_obj_set_style_bg_color(s_echo_mute, c(muted ? T->accent : T->card_bg_2), 0);
    s_syncing = false;

    lvgl_port_unlock();
}
