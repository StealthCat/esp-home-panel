#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t size;
} artwork_svg_t;

artwork_svg_t artwork_get_active_svg(void);
