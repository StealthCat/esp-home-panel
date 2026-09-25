#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PANEL_THEME_CLASSIC_DARK = 0,
    PANEL_THEME_SOCCER_GREEN,
    PANEL_THEME_KITTENS_PINK,
    PANEL_THEME_MAGIC_BLUE,
} panel_theme_id_t;

typedef struct {
    panel_theme_id_t id;
    const char *name;

    uint32_t screen_bg;
    uint32_t screen_bg_2;
    uint32_t card_bg;
    uint32_t card_bg_2;
    uint32_t card_border;
    uint32_t accent;
    uint32_t accent_2;
    uint32_t text;
    uint32_t muted;
    uint32_t online;
    uint32_t warm;

    bool soccer;
    bool kittens;
    bool magic;
} panel_theme_t;

const panel_theme_t *panel_theme_get(void);

