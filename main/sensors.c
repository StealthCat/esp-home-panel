#include "sensors.h"
#include "c4001.h"
#include "board_i2c.h"
#include "settings.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

/* VEML7700 only: explicit opt-in avoids interpreting unrelated I2C devices.
 * Gain 1/8, 100 ms integration; 2025 Vishay application note 84323.
 * No duplicate bus initialization: touch and expander keep their own handles.
 */
static i2c_master_dev_handle_t s_device;
static bool s_configured;
static int64_t s_due_us;
static ambient_light_reading_t s_reading;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

ambient_light_reading_t sensors_ambient_light(void)
{
    portENTER_CRITICAL(&s_lock);
    ambient_light_reading_t reading = s_reading;
    portEXIT_CRITICAL(&s_lock);
    return reading;
}

static void store_reading(bool available, float lux, int64_t now)
{
    portENTER_CRITICAL(&s_lock);
    s_reading = (ambient_light_reading_t){available, lux, now};
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t write_register(uint8_t reg, uint16_t value)
{
    uint8_t bytes[] = {reg, (uint8_t)value, (uint8_t)(value >> 8)};
    return i2c_master_transmit(s_device, bytes, sizeof(bytes), 20);
}

static void ambient_light_poll(void)
{
    const panel_settings_t *cfg = settings_get();
    if (!cfg || !cfg->ambient_light_enabled) return;
    int64_t now = esp_timer_get_time();
    if (now < s_due_us) return;
    s_due_us = now + (int64_t)cfg->sensor_interval_seconds * 1000000;
    i2c_master_bus_handle_t bus = board_7c_i2c_bus();
    if (!bus) return;

    if (!s_device) {
        i2c_device_config_t device = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x10,
            .scl_speed_hz = 100000,
        };
        if (i2c_master_bus_add_device(bus, &device, &s_device) != ESP_OK) return;
    }
    if (!s_configured) {
        /* Disable power saving and configure continuous ALS measurements. */
        if (write_register(0x03, 0) != ESP_OK || write_register(0x00, 0x1000) != ESP_OK) {
            store_reading(false, 0, now);
            return;
        }
        s_configured = true;
        s_due_us = now + 250000; /* integration tolerance + startup, no delay */
        return;
    }
    uint8_t reg = 0x04, bytes[2];
    if (i2c_master_transmit_receive(s_device, &reg, 1, bytes, 2, 20) != ESP_OK) {
        s_configured = false;
        store_reading(false, 0, now);
        return;
    }
    uint16_t raw = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    float lux = raw * 0.5376f;
    if (lux > 1000.0f) {
        lux = (((6.0135e-13f * lux - 9.3924e-9f) * lux + 8.1488e-5f) * lux + 1.0023f) * lux;
    }
    /* Saturation is not a valid measurement. Retry on the normal schedule. */
    store_reading(raw != UINT16_MAX, lux, now);
}

void sensors_poll(void)
{
    ambient_light_poll();
    c4001_poll();
}
