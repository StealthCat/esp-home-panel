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
#include "lvgl.h"
#include "mqtt_bridge.h"
#include "ui_theme.h"
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
    lv_style_set_border_width(&s_card_style, T->magic ? 2 : 1);
    lv_style_set_border_color(&s_card_style, c(T->card_border));
    lv_style_set_border_opa(&s_card_style, T->magic ? 185 : 125);
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

static void add_magic_card_ornaments(lv_obj_t *card);

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
        add_magic_card_ornaments(card);
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
}

static void add_soccer_decor(lv_obj_t *screen)
{
    /* Grass/pitch treatment from the original soccer mockup. */
    for (int i = 0; i < 10; ++i) {
        lv_obj_t *stripe = add_decor_box(screen, -40 + i * 95, 72, 48, 408,
                                         (i % 2) ? 0x0A3A20 : 0x0D4727,
                                         28, 0);
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

    add_decor_box(pitch, 399, 0, 2, 408, 0x50C878, 28, 0);

    lv_obj_t *center = lv_obj_create(pitch);
    lv_obj_set_pos(center, 330, 130);
    lv_obj_set_size(center, 140, 140);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center, 2, 0);
    lv_obj_set_style_border_color(center, c(0x50C878), 0);
    lv_obj_set_style_border_opa(center, 28, 0);

    /* Ball tucked into the upper-right exactly where the mockup places it. */
    add_soccer_ball(screen, 760, -5, 54, 115);
}

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
    add_circle(parent, x + 13, y + 13, 8, color, 240);
    const int px[4] = {10, 22, 10, 0};
    const int py[4] = {0, 10, 22, 10};
    const int pw[4] = {10, 16, 10, 16};
    const int ph[4] = {16, 10, 16, 10};
    for (int i = 0; i < 4; ++i) {
        add_decor_box(parent, x + px[i], y + py[i], pw[i], ph[i],
                      color, 220, 8);
    }
}

static void add_soccer_ball(lv_obj_t *parent, int x, int y, int size, lv_opa_t opa)
{
    add_circle(parent, x, y, size, 0xE8F5EC, opa);
    int p = size / 4;
    add_circle(parent, x + size / 2 - p / 2, y + size / 2 - p / 2, p,
               0x0A2A18, opa);
    const int dx[5] = {size / 2, size - p, size - p / 2, p / 2, 0};
    const int dy[5] = {0, size / 3, size - p, size - p, size / 3};
    for (int i = 0; i < 5; ++i) {
        add_circle(parent, x + dx[i], y + dy[i], p, 0x123820,
                   opa > 35 ? opa - 25 : opa);
    }
}

static void add_magic_card_ornaments(lv_obj_t *card)
{
    const uint32_t gold = T->accent_2;
    const int w = lv_obj_get_width(card);
    const int h = lv_obj_get_height(card);

    /* Four small gold corner brackets matching the original ornate frames. */
    const int xs[4] = {7, w - 28, 7, w - 28};
    const int ys[4] = {7, 7, h - 24, h - 24};
    for (int i = 0; i < 4; ++i) {
        add_decor_box(card, xs[i], ys[i], 21, 2, gold, 175, 1);
        add_decor_box(card, xs[i], ys[i], 2, 17, gold, 175, 1);
    }
}

static void add_lantern(lv_obj_t *parent, int x, int y)
{
    add_circle(parent, x + 3, y + 8, 34, T->warm, 38);
    lv_obj_t *frame = lv_obj_create(parent);
    lv_obj_set_pos(frame, x + 8, y + 7);
    lv_obj_set_size(frame, 23, 38);
    lv_obj_set_style_bg_color(frame, c(0x7B4A13), 0);
    lv_obj_set_style_bg_opa(frame, 70, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, c(T->accent_2), 0);
    lv_obj_set_style_border_opa(frame, 230, 0);
    lv_obj_set_style_radius(frame, 7, 0);
    lv_obj_set_style_pad_all(frame, 0, 0);
    add_circle(parent, x + 13, y + 17, 14, T->warm, 180);
    add_decor_box(parent, x + 16, y + 2, 7, 6, T->accent_2, 220, 2);
    add_decor_box(parent, x + 17, y + 45, 5, 7, T->accent_2, 210, 1);
}

static void add_owl_on_books(lv_obj_t *parent, int x, int y)
{
    /* Books */
    add_decor_box(parent, x, y + 34, 62, 8, 0x7A4E2A, 220, 2);
    add_decor_box(parent, x + 4, y + 42, 56, 7, 0x4B2E18, 220, 2);

    /* Owl body and head */
    add_decor_box(parent, x + 16, y + 15, 27, 25, 0xE8D9B8, 235, 12);
    add_circle(parent, x + 13, y, 34, 0xF1E3C2, 240);
    add_circle(parent, x + 18, y + 8, 9, 0xFFFFFF, 245);
    add_circle(parent, x + 32, y + 8, 9, 0xFFFFFF, 245);
    add_circle(parent, x + 21, y + 11, 4, 0x151A24, 250);
    add_circle(parent, x + 35, y + 11, 4, 0x151A24, 250);
    add_decor_box(parent, x + 27, y + 19, 7, 6, T->warm, 235, 2);
}

static void add_quill_and_ink(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *quill = add_decor_box(parent, x + 17, y, 3, 42,
                                    0xD9C6A0, 210, 2);
    lv_obj_set_style_transform_rotation(quill, 250, 0);
    add_decor_box(parent, x, y + 33, 24, 18, 0x251B2E, 230, 5);
    add_decor_box(parent, x + 4, y + 29, 16, 7, T->accent_2, 190, 3);
}

static void add_cat_face(lv_obj_t *parent, int x, int y, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *face = lv_obj_create(parent);
    lv_obj_set_pos(face, x + 4, y + 6);
    lv_obj_set_size(face, 34, 28);
    lv_obj_set_style_radius(face, 14, 0);
    lv_obj_set_style_bg_color(face, c(color), 0);
    lv_obj_set_style_bg_opa(face, opa, 0);
    lv_obj_set_style_border_width(face, 0, 0);
    lv_obj_set_style_pad_all(face, 0, 0);
    lv_obj_remove_flag(face, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 2; ++i) {
        lv_obj_t *ear = lv_obj_create(parent);
        lv_obj_set_pos(ear, x + 7 + i * 20, y + 1);
        lv_obj_set_size(ear, 12, 12);
        lv_obj_set_style_radius(ear, 3, 0);
        lv_obj_set_style_bg_color(ear, c(color), 0);
        lv_obj_set_style_bg_opa(ear, opa, 0);
        lv_obj_set_style_border_width(ear, 0, 0);
        lv_obj_set_style_transform_rotation(ear, 450, 0);
        lv_obj_set_style_pad_all(ear, 0, 0);
    }

    add_circle(parent, x + 13, y + 17, 4, T->screen_bg, 220);
    add_circle(parent, x + 27, y + 17, 4, T->screen_bg, 220);
    add_circle(parent, x + 20, y + 23, 4, T->accent, 220);

    for (int i = 0; i < 2; ++i) {
        lv_obj_t *whisker = lv_obj_create(parent);
        lv_obj_set_pos(whisker, x + (i ? 24 : 1), y + 25);
        lv_obj_set_size(whisker, 14, 2);
        lv_obj_set_style_bg_color(whisker, c(color), 0);
        lv_obj_set_style_bg_opa(whisker, opa > 50 ? opa - 40 : opa, 0);
        lv_obj_set_style_border_width(whisker, 0, 0);
        lv_obj_set_style_pad_all(whisker, 0, 0);
    }
}

static void add_sleeping_cat(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_set_pos(body, x, y + 11);
    lv_obj_set_size(body, 62, 24);
    lv_obj_set_style_radius(body, 12, 0);
    lv_obj_set_style_bg_color(body, c(T->accent_2), 0);
    lv_obj_set_style_bg_opa(body, 165, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);

    add_cat_face(parent, x + 45, y + 1, T->accent_2, 190);
    add_circle(parent, x + 3, y + 28, 14, T->accent_2, 145);

    lv_obj_t *sleep = make_label(parent, "z  z", x + 12, y - 3,
                                 &lv_font_montserrat_14, T->accent_2);
    lv_obj_set_style_text_opa(sleep, 170, 0);
}

static void add_kittens_decor(lv_obj_t *screen)
{
    /* Header paws/hearts and the cat peeking over the first card match the
     * original kittens composition without baking controls into a bitmap. */
    add_paw(screen, 8, 3, T->accent, 150);
    add_paw(screen, 318, 4, T->accent, 155);
    lv_obj_t *heart = make_label(screen, "<3", 10, 46,
                                 &lv_font_montserrat_14, T->accent);
    lv_obj_set_style_text_opa(heart, 165, 0);

    add_cat_face(screen, 18, 57, T->accent_2, 190);
    add_paw(screen, 2, 448, T->accent, 105);
    add_paw(screen, 776, 448, T->accent, 105);
}

static void add_magic_decor(lv_obj_t *screen)
{
    /* Moon and castle skyline from the original magical night header. */
    add_circle(screen, 548, 4, 58, 0xD4E8FF, 110);
    add_circle(screen, 565, 2, 48, T->screen_bg, 248);

    for (int i = 0; i < 8; ++i) {
        int x = 410 + i * 28;
        int h = 24 + ((i * 17) % 28);
        lv_obj_t *tower = add_decor_box(screen, x, 66 - h, 20, h,
                                        0x06172F, 235, 0);
        (void)tower;
        add_decor_box(screen, x + 4, 60 - h, 12, 10,
                      0x06172F, 235, 0);
        add_decor_box(screen, x + 7, 54 - h, 6, 8,
                      0x06172F, 235, 0);

        if (i % 2 == 0) {
            add_decor_box(screen, x + 6, 52, 3, 5,
                          T->warm, 155, 1);
            add_decor_box(screen, x + 12, 52, 3, 5,
                          T->warm, 155, 1);
        }
    }

    const int sx[12] = {20,75,130,188,250,310,365,610,652,694,744,780};
    const int sy[12] = {7,18,3,32,10,27,6,8,29,5,34,11};
    for (int i = 0; i < 12; ++i) {
        lv_obj_t *star = make_label(screen, "*", sx[i], sy[i],
                                    &lv_font_montserrat_14, T->accent_2);
        lv_obj_set_style_text_opa(star, 115 + (i % 3) * 45, 0);
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
    set_percent_label(s_tapo_pct, value);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        mqtt_bridge_tapo_brightness((uint8_t)value);
    }
}

static void overhead_brightness_cb(lv_event_t *e)
{
    if (s_syncing) return;
    int value = lv_slider_get_value(lv_event_get_target(e));
    set_percent_label(s_overhead_pct, value);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        mqtt_bridge_overhead_brightness((uint8_t)value);
    }
}

static void echo_volume_cb(lv_event_t *e)
{
    if (s_syncing) return;
    int value = lv_slider_get_value(lv_event_get_target(e));
    set_percent_label(s_echo_pct, value);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        mqtt_bridge_echo_volume((uint8_t)value);
    }
}

static void preset_color_cb(lv_event_t *e)
{
    uintptr_t rgb = (uintptr_t)lv_event_get_user_data(e);
    mqtt_bridge_tapo_rgb((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static void overhead_preset_cb(lv_event_t *e)
{
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
    add_chain_icon(card, 20, 20, T->accent_2);
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
        lv_obj_set_style_shadow_width(btn, 3, 0);
        lv_obj_set_style_shadow_opa(btn, 55, 0);
        lv_obj_add_event_cb(btn, preset_color_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)payloads[i]);
    }

    make_label(card, "Brightness", 145, 113, &lv_font_montserrat_16, T->muted);
    make_label(card, "*", 145, 140, &lv_font_montserrat_18, T->accent);

    s_tapo_slider = make_slider(card, 174, 143, 150, T->accent);
    lv_obj_add_event_cb(s_tapo_slider, tapo_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_tapo_slider, tapo_brightness_cb, LV_EVENT_RELEASED, NULL);

    s_tapo_pct = make_label(card, "--%", 328, 139, &lv_font_montserrat_16, T->text);

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
    make_label(card, "Overhead Light", 70, 15, &lv_font_montserrat_22, T->text);
    if (T->magic) {
        add_lantern(card, 18, 4);
    } else {
        add_bulb_icon(card, 23, 13, T->warm);
    }
    make_status_dot(card, &s_overhead_status, 76, 47);

    s_overhead_switch = make_switch(card, 309, 13);
    lv_obj_add_event_cb(s_overhead_switch, switch_overhead_cb, LV_EVENT_VALUE_CHANGED, NULL);

    make_label(card, "Brightness", 20, 78, &lv_font_montserrat_16, T->muted);
    make_label(card, "*", 20, 107, &lv_font_montserrat_18, T->warm);

    s_overhead_slider = make_slider(card, 50, 110, 250, T->warm);
    lv_obj_add_event_cb(s_overhead_slider, overhead_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_overhead_slider, overhead_brightness_cb, LV_EVENT_RELEASED, NULL);

    s_overhead_pct = make_label(card, "--%", 310, 106, &lv_font_montserrat_16, T->text);

    const int pcts[4] = {25, 50, 75, 100};
    for (int i = 0; i < 4; ++i) {
        char txt[8];
        snprintf(txt, sizeof(txt), "%d%%", pcts[i]);
        lv_obj_t *btn = make_button(card, txt, 18 + i * 88, 140, 78, 38);
        lv_obj_add_event_cb(btn, overhead_preset_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)pcts[i]);
    }

    if (T->soccer) {
        lv_obj_t *goal = lv_obj_create(card);
        lv_obj_set_pos(goal, 270, 62);
        lv_obj_set_size(goal, 92, 64);
        lv_obj_set_style_bg_opa(goal, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(goal, 2, 0);
        lv_obj_set_style_border_color(goal, c(T->accent), 0);
        lv_obj_set_style_border_opa(goal, 65, 0);
        lv_obj_set_style_radius(goal, 0, 0);
        for (int i = 1; i < 4; ++i) {
            add_decor_box(goal, i * 21, 0, 1, 64, T->accent, 32, 0);
        }
        for (int i = 1; i < 3; ++i) {
            add_decor_box(goal, 0, i * 20, 92, 1, T->accent, 32, 0);
        }
    } else if (T->kittens) {
        lv_obj_t *caption = make_label(card, "A Brighter\nHappier Home <3", 238, 69,
                                       &lv_font_montserrat_14, T->accent_2);
        lv_obj_set_style_text_opa(caption, 185, 0);
        add_paw(card, 335, 102, T->accent, 90);
    } else if (T->magic) {
        make_label(card, "*", 240, 68, &lv_font_montserrat_18, T->accent_2);
        make_label(card, "*", 305, 82, &lv_font_montserrat_14, T->accent_2);
        make_label(card, "*", 345, 66, &lv_font_montserrat_18, T->accent_2);
        add_decor_box(card, 245, 105, 95, 2, T->accent, 55, 1);
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
    add_mini_fan_icon(card, 20, 14, T->accent);
    make_status_dot(card, &s_fan_status, 76, 47);

    s_fan_icon = create_fan_graphic(card, 25, 65);

    s_fan_switch = make_switch(card, 309, 13);
    lv_obj_add_event_cb(s_fan_switch, switch_fan_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_fan_state_label = make_label(card, "Fan is Off", 230, 116, &lv_font_montserrat_18, T->text);
    make_label(card, "Smart Switch", 230, 143, &lv_font_montserrat_14, T->muted);

    if (T->soccer) {
        add_decor_box(card, 188, 58, 2, 134, T->accent, 38, 0);
        lv_obj_t *center = lv_obj_create(card);
        lv_obj_set_pos(center, 143, 83);
        lv_obj_set_size(center, 92, 92);
        lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(center, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(center, 2, 0);
        lv_obj_set_style_border_color(center, c(T->accent), 0);
        lv_obj_set_style_border_opa(center, 28, 0);
    } else if (T->kittens) {
        add_sleeping_cat(card, 178, 61);
        make_label(card, "z", 215, 47, &lv_font_montserrat_14, T->accent_2);
        make_label(card, "z", 234, 38, &lv_font_montserrat_14, T->accent_2);
        make_label(card, "<3", 329, 79, &lv_font_montserrat_14, T->accent);
        make_label(card, "<3", 159, 88, &lv_font_montserrat_14, T->accent);
        add_paw(card, 20, 156, T->accent, 90);
    } else if (T->magic) {
        lv_obj_set_style_border_color(s_fan_icon, c(T->accent_2), 0);
        lv_obj_t *shield = lv_obj_create(card);
        lv_obj_set_pos(shield, 18, 56);
        lv_obj_set_size(shield, 128, 126);
        lv_obj_set_style_radius(shield, 24, 0);
        lv_obj_set_style_bg_color(shield, c(0x0A2E5F), 0);
        lv_obj_set_style_bg_opa(shield, 65, 0);
        lv_obj_set_style_border_width(shield, 3, 0);
        lv_obj_set_style_border_color(shield, c(T->accent_2), 0);
        lv_obj_set_style_border_opa(shield, 190, 0);
        lv_obj_set_style_pad_all(shield, 0, 0);
        lv_obj_move_background(shield);
        for (int i = 0; i < 6; ++i) {
            add_decor_box(card, 7, 74 + i * 14, 12, 5, T->accent_2, 145, 3);
            add_decor_box(card, 146, 74 + i * 14, 12, 5, T->accent_2, 145, 3);
        }
        make_label(card, "*", 170, 70, &lv_font_montserrat_18, T->accent_2);
        make_label(card, "*", 347, 147, &lv_font_montserrat_18, T->accent_2);
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
    lv_obj_add_event_cb(s_echo_slider, echo_volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_echo_slider, echo_volume_cb, LV_EVENT_RELEASED, NULL);
    s_echo_pct = make_label(card, "--%", 310, 108, &lv_font_montserrat_16, T->text);

    s_echo_mute = make_button(card, LV_SYMBOL_MUTE, 18, 143, 94, 40);
    lv_obj_add_event_cb(s_echo_mute, echo_mute_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *play = make_button(card, LV_SYMBOL_PLAY " " LV_SYMBOL_PAUSE, 126, 143, 96, 40);
    lv_obj_add_event_cb(play, echo_play_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *announce = make_button(card, "Announce", 236, 143, 128, 40);
    lv_obj_add_event_cb(announce, echo_announce_cb, LV_EVENT_CLICKED, NULL);

    if (T->kittens) {
        add_paw(card, 286, 60, T->accent, 90);
        add_paw(card, 338, 72, T->accent_2, 65);
    } else if (T->magic) {
        add_owl_on_books(card, 12, 14);
        add_quill_and_ink(card, 334, 64);
        make_label(card, "*", 280, 60, &lv_font_montserrat_14, T->accent_2);
        make_label(card, "*", 310, 83, &lv_font_montserrat_14, T->accent_2);
    }
}

static void create_header(lv_obj_t *root)
{
    make_label(root, LV_SYMBOL_HOME, 20, 17, &lv_font_montserrat_28, T->accent);
    make_label(root, T->name, 64, 10, &lv_font_montserrat_32, T->text);
    make_label(root, T->tagline, 64, 48, &lv_font_montserrat_14, T->muted);

    s_clock = make_label(root, "--:--", 635, 8, &lv_font_montserrat_28, T->text);
    s_date = make_label(root, "---, --- --", 620, 42, &lv_font_montserrat_14, T->muted);

    s_wifi_icon = make_label(root, LV_SYMBOL_WIFI, 754, 20, &lv_font_montserrat_22, T->muted);

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
    set_percent_label(s_tapo_pct, brightness);
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
    set_percent_label(s_overhead_pct, brightness);
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
    set_percent_label(s_echo_pct, volume);
    lv_label_set_text(s_echo_status, playing ? "Online • Playing" : "Online");
    lv_obj_set_style_bg_color(s_echo_mute, c(muted ? T->accent : T->card_bg_2), 0);
    s_syncing = false;

    lvgl_port_unlock();
}
