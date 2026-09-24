#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    THEME_ART_KITTEN_PEEK = 0,
    THEME_ART_KITTEN_SLEEP,
    THEME_ART_KITTEN_PAWS,
    THEME_ART_SOCCER_BALL,
    THEME_ART_SOCCER_GOAL,
    THEME_ART_MAGIC_CASTLE,
    THEME_ART_MAGIC_LANTERN,
    THEME_ART_MAGIC_FAN_FRAME,
    THEME_ART_MAGIC_OWL,
    THEME_ART_MAGIC_QUILL,
    THEME_ART_MAGIC_CORNER_TL,
    THEME_ART_MAGIC_CORNER_TR,
    THEME_ART_MAGIC_CORNER_BL,
    THEME_ART_MAGIC_CORNER_BR,
} theme_art_id_t;

const lv_image_dsc_t *theme_art_get(theme_art_id_t id);

#ifdef __cplusplus
}
#endif
