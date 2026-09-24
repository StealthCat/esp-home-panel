#include "board_7c.h"

#include <string.h>

#ifdef WOKWI_EMULATOR

#include "esp_check.h"
#include "esp_log.h"
#include "wokwi_panel.h"

static const char *TAG = "board_7c";

esp_err_t board_7c_set_backlight(uint8_t percent)
{
    ESP_LOGD(TAG, "Wokwi virtual backlight: %u%%", percent);
    return ESP_OK;
}

esp_err_t board_7c_init(board_7c_handles_t *handles)
{
    ESP_RETURN_ON_FALSE(handles, ESP_ERR_INVALID_ARG, TAG, "handles is NULL");
    memset(handles, 0, sizeof(*handles));
    ESP_LOGI(TAG, "Initializing Wokwi 7C BOX transport");
    ESP_RETURN_ON_ERROR(wokwi_panel_init(), TAG, "Wokwi panel transport init failed");
    ESP_LOGI(TAG, "Wokwi 7C BOX ready: 800x480 virtual RGB565 display");
    return ESP_OK;
}

#else


#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board_7c";

/* Waveshare ESP32-S3-Touch-LCD-7C-BOX hardware mapping. */
#define I2C_PORT            I2C_NUM_0
#define I2C_SDA             GPIO_NUM_47
#define I2C_SCL             GPIO_NUM_48
#define I2C_FREQ_HZ         100000

#define IOX_ADDR            0x24
#define IOX_REG_MODE        0x02
#define IOX_REG_OUTPUT      0x03
#define IOX_REG_PWM         0x05
#define IOX_TOUCH_RST       1
#define IOX_BACKLIGHT       2

#define TOUCH_INT           GPIO_NUM_4

#define LCD_PCLK_HZ         (16 * 1000 * 1000)
#define LCD_VSYNC           GPIO_NUM_3
#define LCD_HSYNC           GPIO_NUM_46
#define LCD_DE              GPIO_NUM_5
#define LCD_PCLK            GPIO_NUM_7

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_iox;
static uint16_t s_iox_mode = 0xFFFF;
static uint16_t s_iox_output = 0xFFFF;

static esp_err_t iox_write3(uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = { reg, (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    return i2c_master_transmit(s_iox, buf, sizeof(buf), 100);
}

static esp_err_t iox_set_pin(uint8_t pin, bool value)
{
    if (pin > 15) {
        return ESP_ERR_INVALID_ARG;
    }

    if (value) {
        s_iox_output |= (1U << pin);
    } else {
        s_iox_output &= ~(1U << pin);
    }

    return iox_write3(IOX_REG_OUTPUT, s_iox_output);
}

static void log_i2c_devices(void)
{
    static const uint8_t expected[] = { 0x20, 0x24, 0x51, 0x55, 0x5D };
    bool found_any = false;

    ESP_LOGI(TAG, "Scanning known 7C BOX I2C addresses on SDA=%d SCL=%d",
             I2C_SDA, I2C_SCL);

    for (size_t i = 0; i < sizeof(expected); ++i) {
        esp_err_t err = i2c_master_probe(s_i2c_bus, expected[i], 50);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "I2C device ACK at 0x%02X", expected[i]);
            found_any = true;
        } else {
            ESP_LOGW(TAG, "No ACK at I2C address 0x%02X (%s)",
                     expected[i], esp_err_to_name(err));
        }
    }

    if (!found_any) {
        ESP_LOGE(TAG, "No known 7C BOX I2C devices responded on GPIO47/GPIO48");
    }
}

static esp_err_t board_i2c_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus), TAG, "I2C bus init failed");

    /*
     * Probe uses a conservative 100 kHz timing in ESP-IDF. Retry once after a
     * bus reset before declaring the expander absent, then log the other known
     * devices so a hardware/pinout problem is obvious from the serial log.
     */
    esp_err_t probe_err = i2c_master_probe(s_i2c_bus, IOX_ADDR, 100);
    if (probe_err != ESP_OK) {
        ESP_LOGW(TAG, "IO expander did not ACK at 0x%02X; resetting I2C bus and retrying", IOX_ADDR);
        ESP_RETURN_ON_ERROR(i2c_master_bus_reset(s_i2c_bus), TAG, "I2C bus reset failed");
        vTaskDelay(pdMS_TO_TICKS(20));
        probe_err = i2c_master_probe(s_i2c_bus, IOX_ADDR, 100);
    }

    if (probe_err != ESP_OK) {
        log_i2c_devices();
        ESP_LOGE(TAG, "IO expander 0x%02X not found", IOX_ADDR);
        return probe_err;
    }

    ESP_LOGI(TAG, "IO expander ACK at 0x%02X", IOX_ADDR);

    const i2c_device_config_t iox_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = IOX_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &iox_cfg, &s_iox), TAG, "IO expander add failed");

    esp_err_t err = iox_write3(IOX_REG_MODE, s_iox_mode);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Initial IO expander mode write failed (%s); resetting bus and retrying",
                 esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(i2c_master_bus_reset(s_i2c_bus), TAG, "I2C bus reset failed");
        vTaskDelay(pdMS_TO_TICKS(20));
        err = iox_write3(IOX_REG_MODE, s_iox_mode);
    }
    ESP_RETURN_ON_ERROR(err, TAG, "IO expander mode write failed");
    ESP_RETURN_ON_ERROR(iox_write3(IOX_REG_OUTPUT, s_iox_output), TAG, "IO expander output write failed");

    return ESP_OK;
}

esp_err_t board_7c_set_backlight(uint8_t percent)
{
    if (!s_iox) {
        return ESP_ERR_INVALID_STATE;
    }

    if (percent == 0) {
        return iox_set_pin(IOX_BACKLIGHT, false);
    }

    if (percent < 5) {
        percent = 5;
    }
    if (percent > 100) {
        percent = 100;
    }

    const uint8_t pwm = (uint8_t)((percent * 255U) / 100U);
    uint8_t buf[2] = { IOX_REG_PWM, pwm };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_iox, buf, sizeof(buf), 100), TAG, "Backlight PWM write failed");
    return iox_set_pin(IOX_BACKLIGHT, true);
}

static esp_err_t touch_reset_sequence(void)
{
    gpio_config_t int_cfg = {
        .pin_bit_mask = 1ULL << TOUCH_INT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_cfg), TAG, "Touch INT GPIO config failed");

    /* Select GT911 address 0x5D exactly as Waveshare's reference sequence. */
    ESP_RETURN_ON_ERROR(iox_set_pin(IOX_TOUCH_RST, false), TAG, "Touch reset low failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(gpio_set_level(TOUCH_INT, 0), TAG, "Touch INT low failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(iox_set_pin(IOX_TOUCH_RST, true), TAG, "Touch reset high failed");
    vTaskDelay(pdMS_TO_TICKS(200));

    return ESP_OK;
}

static esp_err_t touch_init(esp_lcd_touch_handle_t *out_touch)
{
    ESP_RETURN_ON_ERROR(touch_reset_sequence(), TAG, "Touch reset sequence failed");

    esp_lcd_panel_io_handle_t touch_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &touch_io), TAG, "GT911 panel IO init failed");

    static esp_lcd_touch_io_gt911_config_t gt911_cfg;
    gt911_cfg.dev_addr = io_cfg.dev_addr;

    const esp_lcd_touch_config_t touch_cfg = {
        .x_max = PANEL_H_RES,
        .y_max = PANEL_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .driver_data = &gt911_cfg,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(touch_io, &touch_cfg, out_touch), TAG, "GT911 init failed");
    return ESP_OK;
}

static esp_err_t lcd_init(esp_lcd_panel_handle_t *out_panel)
{
    const esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = PANEL_H_RES,
            .v_res = PANEL_V_RES,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 2,
        .bounce_buffer_size_px = PANEL_H_RES * 10,
        .sram_trans_align = 4,
        .dma_burst_size = 64,

        .hsync_gpio_num = LCD_HSYNC,
        .vsync_gpio_num = LCD_VSYNC,
        .de_gpio_num = LCD_DE,
        .pclk_gpio_num = LCD_PCLK,
        .disp_gpio_num = GPIO_NUM_NC,

        .data_gpio_nums = {
            GPIO_NUM_14, /* B3 */
            GPIO_NUM_38, /* B4 */
            GPIO_NUM_18, /* B5 */
            GPIO_NUM_17, /* B6 */
            GPIO_NUM_10, /* B7 */
            GPIO_NUM_39, /* G2 */
            GPIO_NUM_0,  /* G3 */
            GPIO_NUM_45, /* G4 */
            GPIO_NUM_9,  /* G5 */
            GPIO_NUM_8,  /* G6 */
            GPIO_NUM_21, /* G7 */
            GPIO_NUM_1,  /* R3 */
            GPIO_NUM_2,  /* R4 */
            GPIO_NUM_42, /* R5 */
            GPIO_NUM_41, /* R6 */
            GPIO_NUM_40, /* R7 */
        },
        .flags.fb_in_psram = true,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_cfg, out_panel), TAG, "RGB panel creation failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out_panel), TAG, "RGB panel init failed");
    return ESP_OK;
}

esp_err_t board_7c_init(board_7c_handles_t *handles)
{
    ESP_RETURN_ON_FALSE(handles, ESP_ERR_INVALID_ARG, TAG, "handles is NULL");
    memset(handles, 0, sizeof(*handles));

    ESP_LOGI(TAG, "Initializing Waveshare 7C BOX hardware");

    ESP_RETURN_ON_ERROR(board_i2c_init(), TAG, "I2C / IO expander init failed");
    ESP_RETURN_ON_ERROR(board_7c_set_backlight(0), TAG, "Backlight off failed");
    ESP_RETURN_ON_ERROR(touch_init(&handles->touch), TAG, "Touch init failed");
    ESP_RETURN_ON_ERROR(lcd_init(&handles->panel), TAG, "LCD init failed");

    ESP_RETURN_ON_ERROR(board_7c_set_backlight(CONFIG_PANEL_BACKLIGHT), TAG, "Backlight on failed");

    ESP_LOGI(TAG, "7C BOX ready: 800x480 RGB565 + GT911");
    return ESP_OK;
}

#endif
