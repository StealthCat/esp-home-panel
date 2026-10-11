#include "c4001.h"
#include "board_i2c.h"
#include "settings.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

/* SEN0610 register protocol: DFRobot/DFRobot_C4001, src/DFRobot_C4001.*.
 * Presence mode only. All multi-byte parameters are little-endian.
 * Commands settle across loop iterations; no blocking conversion delays.
 */
#define REG_STATUS 0x00
#define REG_CONTROL 0x01
#define REG_COMMAND 0x02
#define REG_RESULT 0x10
#define REG_CONFIG 0x20
#define CMD_STOP 0x33
#define CMD_START 0x55
#define CMD_SAVE 0x5c
#define CMD_CHANGE_MODE 0x3b

typedef enum { CHECK_MODE, STOPPED, VERIFY_CONFIG, STARTED, RUNNING } stage_t;
static stage_t s_stage;
static i2c_master_dev_handle_t s_device;
static int64_t s_due_us = 2000000; /* let the radar boot before configuration */
static presence_reading_t s_reading;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

presence_reading_t c4001_reading(void)
{
    portENTER_CRITICAL(&s_lock);
    presence_reading_t reading = s_reading;
    portEXIT_CRITICAL(&s_lock);
    return reading;
}

static void store_reading(bool available, bool occupied, int64_t now)
{
    portENTER_CRITICAL(&s_lock);
    s_reading = (presence_reading_t){available, occupied, now};
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t read_register(uint8_t reg, uint8_t *data, size_t size)
{
    return i2c_master_transmit_receive(s_device, &reg, 1, data, size, 20);
}

static esp_err_t command(uint8_t reg, uint8_t value)
{
    uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(s_device, bytes, sizeof(bytes), 20);
}

static void desired_config(const panel_settings_t *cfg, uint8_t data[11])
{
    uint16_t max_cm = (uint16_t)cfg->presence_max_m * 100;
    uint16_t hold = (uint16_t)cfg->presence_hold_seconds * 2; /* 0.5 s units */
    const uint8_t bytes[11] = {
        cfg->presence_trigger_sensitivity, cfg->presence_keep_sensitivity,
        0, /* immediate trigger; vacancy delay is handled by the radar */
        (uint8_t)hold, (uint8_t)(hold >> 8),
        30, 0, /* 0.3 m minimum */
        (uint8_t)max_cm, (uint8_t)(max_cm >> 8),
        (uint8_t)max_cm, (uint8_t)(max_cm >> 8), /* same trigger range */
    };
    memcpy(data, bytes, sizeof(bytes));
}

void c4001_poll(void)
{
    const panel_settings_t *cfg = settings_get();
    if (!cfg || !cfg->presence_enabled) return;
    int64_t now = esp_timer_get_time();
    if (now < s_due_us) return;
    s_due_us = now + 500000;
    i2c_master_bus_handle_t bus = board_7c_i2c_bus();
    if (!bus) return;
    if (!s_device) {
        i2c_device_config_t device = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = cfg->presence_address,
            .scl_speed_hz = 100000,
        };
        if (i2c_master_bus_add_device(bus, &device, &s_device) != ESP_OK) goto failed;
    }

    uint8_t status, result, actual[11], desired[11];
    desired_config(cfg, desired);
    switch (s_stage) {
    case CHECK_MODE:
        if (read_register(REG_STATUS, &status, 1) != ESP_OK || !(status & 0x80)) goto failed;
        if (status & 0x02) {
            /* This command toggles modes. Always read status before issuing it. */
            if (command(REG_COMMAND, CMD_CHANGE_MODE) != ESP_OK) goto failed;
            s_due_us = now + 1500000;
            return;
        }
        if (command(REG_CONTROL, CMD_STOP) != ESP_OK) goto failed;
        s_stage = STOPPED;
        s_due_us = now + 200000;
        return;
    case STOPPED:
        if (read_register(REG_STATUS, &status, 1) != ESP_OK || (status & 0x83) != 0x80) goto failed;
        if (read_register(REG_CONFIG, actual, sizeof(actual)) != ESP_OK) goto failed;
        if (memcmp(actual, desired, sizeof(actual)) != 0) {
            uint8_t bytes[12] = {REG_CONFIG};
            memcpy(bytes + 1, desired, sizeof(desired));
            if (i2c_master_transmit(s_device, bytes, sizeof(bytes), 20) != ESP_OK ||
                command(REG_COMMAND, CMD_SAVE) != ESP_OK) goto failed;
        }
        s_stage = VERIFY_CONFIG;
        s_due_us = now + 500000;
        return;
    case VERIFY_CONFIG:
        if (read_register(REG_CONFIG, actual, sizeof(actual)) != ESP_OK ||
            memcmp(actual, desired, sizeof(actual)) != 0) goto failed;
        if (command(REG_CONTROL, CMD_START) != ESP_OK) goto failed;
        s_stage = STARTED;
        s_due_us = now + 200000;
        return;
    case STARTED:
    case RUNNING:
        if (read_register(REG_STATUS, &status, 1) != ESP_OK || (status & 0x83) != 0x81 ||
            read_register(REG_RESULT, &result, 1) != ESP_OK) goto failed;
        store_reading(true, (result & 1) != 0, now);
        s_stage = RUNNING;
        return;
    }
failed:
    /* A missing/stopped/rebooting radar is unavailable, never a vacant room. */
    store_reading(false, false, now);
    s_stage = CHECK_MODE;
    s_due_us = now + 5000000;
}
