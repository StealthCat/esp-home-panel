#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define WEATHER_MAX_FIELDS 48
#define WEATHER_DAYS 5
#define WEATHER_MAX_PAYLOAD 24576

typedef struct { char label[80], value[40], unit[24]; } weather_field_t;
typedef struct { char date[24], condition[40], high[32], low[32], rain[40]; } weather_day_t;
typedef struct {
    uint32_t revision;
    int64_t station_received_us, forecast_received_us;
    unsigned station_age_seconds;
    bool station_available, forecast_available, truncated;
    char name[80];
    unsigned field_count, day_count;
    weather_field_t fields[WEATHER_MAX_FIELDS];
    weather_day_t days[WEATHER_DAYS];
} weather_snapshot_t;

bool weather_init(void);
bool weather_copy(weather_snapshot_t *out);
/* Only the MQTT task writes; UI reads a coherent copy. */
bool weather_receive(bool forecast, const char *data, size_t length, int64_t now);
