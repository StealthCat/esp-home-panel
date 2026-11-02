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

/* Last radar connection/initialization result, updated in the polling task.
 * The reason points to a static string and is safe to use after the snapshot. */
typedef struct {
    bool responding;
    bool alternate_address;
    bool status_valid;
    uint8_t raw_status;
    const char *reason;
} presence_diagnostic_t;

presence_diagnostic_t c4001_diagnostic(void);
