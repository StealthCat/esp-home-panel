#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#define WEATHER_MAX_FIELDS 48
#define WEATHER_DAYS 5
#define WEATHER_HOURS 12
#define WEATHER_MAX_PAYLOAD (96 * 1024)

typedef enum { WX_CLEAR, WX_PARTLY, WX_CLOUD, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM, WX_WIND, WX_UNKNOWN } weather_icon_t;
typedef struct { char label[64], value[40], unit[16]; } weather_field_t;
/* All numeric values are SI-friendly: Celsius, km/h, hPa, millimetres.
 * NAN means missing; zero is always a real measurement. */
typedef struct {
    float temp, humidity, feels, dew, heat_index, wind_chill;
    float wind, gust, direction, pressure, rain_rate, rain_day, uv, solar;
} weather_current_t;
typedef struct {
    time_t date, sunrise, sunset;
    float high, low, rain, rain_chance, wind, uv;
    weather_icon_t icon;
    char narrative[224];
} weather_day_t;
typedef struct { time_t time; float temp, rain_chance, rain; weather_icon_t icon; } weather_hour_t;
typedef struct {
    uint32_t revision;
    int64_t station_received_us, forecast_received_us;
    time_t observed_at;
    bool station_available, forecast_available, truncated;
    char name[80], station_status[96], forecast_status[96];
    double latitude, longitude;
    weather_current_t current;
    unsigned field_count, day_count, hour_count;
    weather_field_t fields[WEATHER_MAX_FIELDS];
    weather_day_t days[WEATHER_DAYS];
    weather_hour_t hours[WEATHER_HOURS];
} weather_snapshot_t;

void weather_reset(weather_snapshot_t *out);
bool weather_init(void);
void weather_start(void);
bool weather_copy(weather_snapshot_t *out);
const char *weather_icon_name(weather_icon_t icon);
void weather_format(char *dst, size_t size, float value, const char *kind, bool metric);
/* Pure parsers: modify the candidate only; the worker publishes on success. */
bool weather_parse_station(weather_snapshot_t *out, const char *json, size_t length,
    bool ambient, const char *station, bool metric);
bool weather_parse_forecast(weather_snapshot_t *out, const char *json, size_t length,
    bool wunderground, time_t now);
bool weather_encode(char *dst, size_t size, const char *value);
unsigned weather_retry_seconds(unsigned failures, int status, unsigned retry_after);
