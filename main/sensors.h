#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool available;
    float lux;
    int64_t sampled_us;
} ambient_light_reading_t;

/* Called from the application loop; never waits for an integration cycle. */
void sensors_poll(void);
ambient_light_reading_t sensors_ambient_light(void);
