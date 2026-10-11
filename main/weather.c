#include "weather.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"

static weather_snapshot_t *s_weather;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

bool weather_init(void)
{
    if (s_weather) return true;
    s_weather = heap_caps_calloc(1, sizeof(*s_weather), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_weather) s_weather = calloc(1, sizeof(*s_weather));
    return s_weather != NULL;
}
bool weather_copy(weather_snapshot_t *out)
{
    if (!out || !s_weather) return false;
    portENTER_CRITICAL(&s_lock);
    memcpy(out, s_weather, sizeof(*out));
    portEXIT_CRITICAL(&s_lock);
    return true;
}
static void string_field(cJSON *obj, const char *key, char *dst, size_t size)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    const char *text = cJSON_IsString(v) ? v->valuestring : "";
    size_t n = strlen(text);
    if (n >= size) {
        n = size - 1;
        /* Never leave half of a UTF-8 character at a fixed-buffer boundary. */
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, text, n);
    dst[n] = 0;
    for (size_t i = 0; i < n; ++i) if ((unsigned char)dst[i] < 32) dst[i] = ' ';
}
bool weather_receive(bool forecast, const char *data, size_t length, int64_t now)
{
    if (!s_weather || !data || !length || length > WEATHER_MAX_PAYLOAD) return false;
    cJSON *root = cJSON_ParseWithLength(data, length);
    if (!cJSON_IsObject(root)) { cJSON_Delete(root); return false; }
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *available = cJSON_GetObjectItemCaseSensitive(root, "available");
    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, forecast ? "days" : "fields");
    if (!cJSON_IsNumber(version) || version->valueint != 1 || !cJSON_IsBool(available) || !cJSON_IsArray(items)) {
        cJSON_Delete(root); return false;
    }
    weather_snapshot_t *next = heap_caps_calloc(1, sizeof(*next), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!next) { cJSON_Delete(root); return false; }
    weather_copy(next);
    if (forecast) {
        next->forecast_received_us = now;
        next->forecast_available = cJSON_IsTrue(available);
        next->day_count = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, items) {
            if (!cJSON_IsObject(item) || next->day_count == WEATHER_DAYS) break;
            weather_day_t *day = &next->days[next->day_count++];
            string_field(item, "date", day->date, sizeof(day->date));
            string_field(item, "condition", day->condition, sizeof(day->condition));
            string_field(item, "high", day->high, sizeof(day->high));
            string_field(item, "low", day->low, sizeof(day->low));
            string_field(item, "rain", day->rain, sizeof(day->rain));
        }
    } else {
        next->station_received_us = now;
        next->station_available = cJSON_IsTrue(available);
        string_field(root, "name", next->name, sizeof(next->name));
        cJSON *age = cJSON_GetObjectItemCaseSensitive(root, "age_seconds");
        next->station_age_seconds = cJSON_IsNumber(age) && isfinite(age->valuedouble) && age->valuedouble >= 0 && age->valuedouble <= 86400 ? (unsigned)age->valuedouble : 86400;
        next->truncated = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "truncated")) || cJSON_GetArraySize(items) > WEATHER_MAX_FIELDS;
        next->field_count = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, items) {
            if (!cJSON_IsObject(item) || next->field_count == WEATHER_MAX_FIELDS) break;
            weather_field_t *field = &next->fields[next->field_count++];
            string_field(item, "label", field->label, sizeof(field->label));
            string_field(item, "value", field->value, sizeof(field->value));
            string_field(item, "unit", field->unit, sizeof(field->unit));
        }
    }
    ++next->revision;
    portENTER_CRITICAL(&s_lock);
    weather_snapshot_t *old = s_weather;
    s_weather = next;
    portEXIT_CRITICAL(&s_lock);
    free(old);
    cJSON_Delete(root);
    return true;
}
