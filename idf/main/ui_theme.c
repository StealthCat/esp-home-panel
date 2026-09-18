#include "ui_theme.h"

static const panel_theme_t THEME_CLASSIC = {
    .id = PANEL_THEME_CLASSIC_DARK,
    .name = "Home Control",
    .tagline = "Smart Home  •  Powered by ESP-IDF + LVGL",
    .screen_bg = 0x07111C,
    .screen_bg_2 = 0x0B1D31,
    .card_bg = 0x101D2A,
    .card_bg_2 = 0x172A3A,
    .card_border = 0x35536D,
    .accent = 0x21A7FF,
    .accent_2 = 0x8A5CFF,
    .text = 0xF7FBFF,
    .muted = 0xA7BCD0,
    .online = 0x34E66A,
    .warm = 0xFFD447,
};

static const panel_theme_t THEME_SOCCER = {
    .id = PANEL_THEME_SOCCER_GREEN,
    .name = "Home Control",
    .tagline = "Home Field  •  Fast Local Control",
    .screen_bg = 0x03150D,
    .screen_bg_2 = 0x08301C,
    .card_bg = 0x082517,
    .card_bg_2 = 0x0D3A22,
    .card_border = 0x31B45F,
    .accent = 0x39EB69,
    .accent_2 = 0xB2FF59,
    .text = 0xF4FFF7,
    .muted = 0xA4D6B1,
    .online = 0x35EF69,
    .warm = 0xE7FF55,
    .soccer = true,
};

static const panel_theme_t THEME_KITTENS = {
    .id = PANEL_THEME_KITTENS_PINK,
    .name = "Home Control",
    .tagline = "Cozy Controls  •  Purrfectly Connected",
    .screen_bg = 0x1B1017,
    .screen_bg_2 = 0x3A1F2C,
    .card_bg = 0x35202B,
    .card_bg_2 = 0x5A3041,
    .card_border = 0xFF85B6,
    .accent = 0xFF69AA,
    .accent_2 = 0xFFC0DA,
    .text = 0xFFF6FA,
    .muted = 0xEAB6CC,
    .online = 0x4AF17A,
    .warm = 0xFFB1CC,
    .kittens = true,
};

static const panel_theme_t THEME_MAGIC = {
    .id = PANEL_THEME_MAGIC_BLUE,
    .name = "Home Control",
    .tagline = "A Smarter Home  •  More Magical Living",
    .screen_bg = 0x04132B,
    .screen_bg_2 = 0x082A55,
    .card_bg = 0x071D3E,
    .card_bg_2 = 0x0D3266,
    .card_border = 0xC89B46,
    .accent = 0x2FA8FF,
    .accent_2 = 0xE2B860,
    .text = 0xFFFDF7,
    .muted = 0xAEC9EA,
    .online = 0x40F07A,
    .warm = 0xF5C95A,
    .magic = true,
};

const panel_theme_t *panel_theme_get(void)
{
#if CONFIG_PANEL_THEME_SOCCER
    return &THEME_SOCCER;
#elif CONFIG_PANEL_THEME_KITTENS
    return &THEME_KITTENS;
#elif CONFIG_PANEL_THEME_MAGIC
    return &THEME_MAGIC;
#else
    return &THEME_CLASSIC;
#endif
}
