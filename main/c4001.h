#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool available;
    bool occupied;
    int64_t sampled_us;
} presence_reading_t;

void c4001_poll(void);
presence_reading_t c4001_reading(void);
