#include "board_7c.h"\n#include "settings.h"

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

#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         100000

#define BOX_I2C_SDA         GPIO_NUM_47
#define BOX_I2C_SCL         GPIO_NUM_48
#define LCD7_I2C_SDA        GPIO_NUM_8
#define LCD7_I2C_SCL        GPIO_NUM_9

/* 7C-BOX register-mapped IO extension controller. */
#define BOX_IOX_ADDR        0x24
#define BOX_IOX_REG_MODE    0x02
#define BOX_IOX_REG_OUTPUT  0x03
#define BOX_IOX_REG_PWM     0x05
#define BOX_LCD_RST         0
#define BOX_TOUCH_RST       1
#define BOX_BACKLIGHT       2
#define BOX_LCD_VDD_EN      5

/* ESP32-S3-Touch-LCD-7 CH422G command addresses. */
#define CH422G_WR_SET_ADDR   (0x48 >> 1) /* 0x24 */
#define CH422G_WR_IO_ADDR    (0x70 >> 1) /* 0x38 */
#define CH422G_IO_OE         0x01
#define LCD7_TOUCH_RST       1
#define LCD7_BACKLIGHT       2
#define LCD7_LCD_RST         3

#define TOUCH_INT           GPIO_NUM_4

#define LCD_PCLK_HZ         (16 * 1000 * 1000)
#define LCD_VSYNC           GPIO_NUM_3
#define LCD_HSYNC           GPIO_NUM_46
#define LCD_DE              GPIO_NUM_5
#define LCD_PCLK            GPIO_NUM_7

typedef enum {
    HW_PROFILE_UNKNOWN = 0,
    HW_PROFILE_7C_BOX,
    HW_PROFILE_TOUCH_LCD_7,
} hw_profile_t;

static hw_profile_t s_hw_profile;
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_box_iox;
static i2c_master_dev_handle_t s_ch422g_wr_set;
static i2c_master_dev_handle_t s_ch422g_wr_io;
static uint16_t s_box_output = 0xFFFF;
static uint8_t s_ch422g_output = 0xFF;
static gpio_num_t s_i2c_sda = GPIO_NUM_NC;
static gpio_num_t s_i2c_scl = GPIO_NUM_NC;

static esp_err_t create_i2c_bus(gpio_num_t sda, gpio_num_t scl)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err == ESP_OK) {
        s_i2c_sda = sda;
        s_i2c_scl = scl;
    }
    return err;
}

static void destroy_i2c_bus(void)
{
    if (s_i2c_bus) {
        esp_err_t err = i2c_del_master_bus(s_i2c_bus);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Unable to delete probe I2C bus: %s", esp_err_to_name(err));
        }
    }
    s_i2c_bus = NULL;
    s_i2c_sda = GPIO_NUM_NC;
    s_i2c_scl = GPIO_NUM_NC;
}

static esp_err_t add_i2c_device(uint8_t address, i2c_master_dev_handle_t *handle)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_i2c_bus, &cfg, handle);
}

static esp_err_t box_write3(uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = { reg, (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    return i2c_master_transmit(s_box_iox, buf, sizeof(buf), 100);
}

static esp_err_t ch422g_write(i2c_master_dev_handle_t dev, uint8_t value)
{
    return i2c_master_transmit(dev, &value, 1, 100);
}

static esp_err_t iox_set_pin(uint8_t pin, bool value)
{
    if (s_hw_profile == HW_PROFILE_7C_BOX) {
        if (pin > 15) {
            return ESP_ERR_INVALID_ARG;
        }

        if (value) {
            s_box_output |= (1U << pin);
        } else {
            s_box_output &= ~(1U << pin);
        }
        return box_write3(BOX_IOX_REG_OUTPUT, s_box_output);
    }

    if (s_hw_profile == HW_PROFILE_TOUCH_LCD_7) {
        if (pin > 7) {
            return ESP_ERR_INVALID_ARG;
        }

        if (value) {
            s_ch422g_output |= (1U << pin);
        } else {
            s_ch422g_output &= ~(1U << pin);
        }
        return ch422g_write(s_ch422g_wr_io, s_ch422g_output);
    }

    return ESP_ERR_INVALID_STATE;
}

static void log_i2c_devices(void)
{
    static const uint8_t addresses[] = {
        0x14, 0x20, 0x23, 0x24, 0x26, 0x38, 0x51, 0x55, 0x5D
    };
    bool found_any = false;

    ESP_LOGI(TAG, "Scanning I2C bus on SDA=%d SCL=%d", s_i2c_sda, s_i2c_scl);
    for (size_t i = 0; i < sizeof(addresses); ++i) {
        esp_err_t err = i2c_master_probe(s_i2c_bus, addresses[i], 50);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "I2C ACK at 0x%02X", addresses[i]);
            found_any = true;
        }
    }

    if (!found_any) {
        ESP_LOGW(TAG, "No known Waveshare 7-inch devices responded on this bus");
    }
}

static esp_err_t detect_and_init_i2c(void)
{
    /*
     * Newer 7C-BOX:
     *   I2C SDA/SCL = GPIO47/GPIO48
     *   RGB G5/G6   = GPIO9/GPIO8
     *   IO extension controller responds at 0x24 as a register-mapped device.
     *
     * Earlier/non-BOX ESP32-S3-Touch-LCD-7:
     *   I2C SDA/SCL = GPIO8/GPIO9
     *   RGB G5/G6   = GPIO48/GPIO47
     *   CH422G uses command addresses such as 0x24 (WR_SET) and 0x38 (WR_IO).
     */
    ESP_RETURN_ON_ERROR(create_i2c_bus(BOX_I2C_SDA, BOX_I2C_SCL), TAG,
                        "7C-BOX I2C probe bus init failed");

    if (i2c_master_probe(s_i2c_bus, BOX_IOX_ADDR, 100) == ESP_OK) {
        s_hw_profile = HW_PROFILE_7C_BOX;
        ESP_LOGI(TAG, "Detected Waveshare ESP32-S3-Touch-LCD-7C-BOX hardware");

        ESP_RETURN_ON_ERROR(add_i2c_device(BOX_IOX_ADDR, &s_box_iox), TAG,
                            "7C-BOX IO expander add failed");
        ESP_RETURN_ON_ERROR(box_write3(BOX_IOX_REG_MODE, 0xFFFF), TAG,
                            "7C-BOX IO expander mode write failed");
        ESP_RETURN_ON_ERROR(box_write3(BOX_IOX_REG_OUTPUT, s_box_output), TAG,
                            "7C-BOX IO expander output write failed");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "No 7C-BOX expander on GPIO47/GPIO48; trying ESP32-S3-Touch-LCD-7 layout");
    log_i2c_devices();
    destroy_i2c_bus();

    ESP_RETURN_ON_ERROR(create_i2c_bus(LCD7_I2C_SDA, LCD7_I2C_SCL), TAG,
                        "Touch-LCD-7 I2C bus init failed");

    esp_err_t wr_set_probe = i2c_master_probe(s_i2c_bus, CH422G_WR_SET_ADDR, 100);
    esp_err_t wr_io_probe = i2c_master_probe(s_i2c_bus, CH422G_WR_IO_ADDR, 100);
    if (wr_set_probe != ESP_OK && wr_io_probe != ESP_OK) {
        log_i2c_devices();
        ESP_LOGE(TAG, "Neither supported Waveshare 7-inch hardware profile was detected");
        return ESP_ERR_NOT_FOUND;
    }

    s_hw_profile = HW_PROFILE_TOUCH_LCD_7;
    ESP_LOGI(TAG, "Detected Waveshare ESP32-S3-Touch-LCD-7 (CH422G) hardware");

    ESP_RETURN_ON_ERROR(add_i2c_device(CH422G_WR_SET_ADDR, &s_ch422g_wr_set), TAG,
                        "CH422G WR_SET device add failed");
    ESP_RETURN_ON_ERROR(add_i2c_device(CH422G_WR_IO_ADDR, &s_ch422g_wr_io), TAG,
                        "CH422G WR_IO device add failed");

    /* Enable IO0..IO7 outputs and drive the initial all-high shadow state. */
    ESP_RETURN_ON_ERROR(ch422g_write(s_ch422g_wr_set, CH422G_IO_OE), TAG,
                        "CH422G output-enable write failed");
    ESP_RETURN_ON_ERROR(ch422g_write(s_ch422g_wr_io, s_ch422g_output), TAG,
                        "CH422G output write failed");

    return ESP_OK;
}

esp_err_t board_7c_set_backlight(uint8_t percent)
{
    if (s_hw_profile == HW_PROFILE_UNKNOWN) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_hw_profile == HW_PROFILE_TOUCH_LCD_7) {
        /* CH422G board provides switch control, not PWM brightness control. */
        return iox_set_pin(LCD7_BACKLIGHT, percent > 0);
    }

    if (!s_box_iox) {
        return ESP_ERR_INVALID_STATE;
    }

    if (percent == 0) {
        return iox_set_pin(BOX_BACKLIGHT, false);
    }

    if (percent < 5) {
        percent = 5;
    }
    if (percent > 100) {
        percent = 100;
    }

    const uint8_t pwm = (uint8_t)((percent * 255U) / 100U);
    uint8_t buf[2] = { BOX_IOX_REG_PWM, pwm };
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_box_iox, buf, sizeof(buf), 100), TAG,
                        "Backlight PWM write failed");
    return iox_set_pin(BOX_BACKLIGHT, true);
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

    const uint8_t rst_pin = (s_hw_profile == HW_PROFILE_7C_BOX) ? BOX_TOUCH_RST : LCD7_TOUCH_RST;

    /* INT low while reset is released selects the GT911 0x5D address. */
    ESP_RETURN_ON_ERROR(iox_set_pin(rst_pin, false), TAG, "Touch reset low failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(gpio_set_level(TOUCH_INT, 0), TAG, "Touch INT low failed");
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_RETURN_ON_ERROR(iox_set_pin(rst_pin, true), TAG, "Touch reset high failed");
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Return INT to input mode before the GT911 driver attaches to it. */
    ESP_RETURN_ON_ERROR(gpio_set_direction(TOUCH_INT, GPIO_MODE_INPUT), TAG,
                        "Touch INT input mode failed");

    return ESP_OK;
}

static esp_err_t touch_init(esp_lcd_touch_handle_t *out_touch)
{
    ESP_RETURN_ON_ERROR(touch_reset_sequence(), TAG, "Touch reset sequence failed");

    esp_lcd_panel_io_handle_t touch_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.scl_speed_hz = I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &touch_io), TAG,
                        "GT911 panel IO init failed");

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

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(touch_io, &touch_cfg, out_touch), TAG,
                        "GT911 init failed");
    return ESP_OK;
}

static esp_err_t lcd_control_init(void)
{
    if (s_hw_profile == HW_PROFILE_TOUCH_LCD_7) {
        ESP_RETURN_ON_ERROR(iox_set_pin(LCD7_LCD_RST, false), TAG, "LCD reset low failed");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(iox_set_pin(LCD7_LCD_RST, true), TAG, "LCD reset high failed");
        vTaskDelay(pdMS_TO_TICKS(100));
        return ESP_OK;
    }

    if (s_hw_profile == HW_PROFILE_7C_BOX) {
        ESP_RETURN_ON_ERROR(iox_set_pin(BOX_LCD_VDD_EN, true), TAG, "LCD VDD enable failed");
        ESP_RETURN_ON_ERROR(iox_set_pin(BOX_LCD_RST, false), TAG, "LCD reset low failed");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(iox_set_pin(BOX_LCD_RST, true), TAG, "LCD reset high failed");
        vTaskDelay(pdMS_TO_TICKS(100));
        return ESP_OK;
    }

    return ESP_ERR_INVALID_STATE;
}

static esp_err_t lcd_init(esp_lcd_panel_handle_t *out_panel)
{
    ESP_RETURN_ON_ERROR(lcd_control_init(), TAG, "LCD control initialization failed");

    const gpio_num_t data8 =
        (s_hw_profile == HW_PROFILE_7C_BOX) ? GPIO_NUM_9 : GPIO_NUM_48;
    const gpio_num_t data9 =
        (s_hw_profile == HW_PROFILE_7C_BOX) ? GPIO_NUM_8 : GPIO_NUM_47;

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
            data8,       /* G5 */
            data9,       /* G6 */
            GPIO_NUM_21, /* G7 */
            GPIO_NUM_1,  /* R3 */
            GPIO_NUM_2,  /* R4 */
            GPIO_NUM_42, /* R5 */
            GPIO_NUM_41, /* R6 */
            GPIO_NUM_40, /* R7 */
        },
        .flags.fb_in_psram = true,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_cfg, out_panel), TAG,
                        "RGB panel creation failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out_panel), TAG, "RGB panel init failed");
    return ESP_OK;
}

esp_err_t board_7c_init(board_7c_handles_t *handles)
{
    ESP_RETURN_ON_FALSE(handles, ESP_ERR_INVALID_ARG, TAG, "handles is NULL");
    memset(handles, 0, sizeof(*handles));

    ESP_LOGI(TAG, "Initializing Waveshare 7-inch panel hardware");

    ESP_RETURN_ON_ERROR(detect_and_init_i2c(), TAG, "Hardware profile / I2C init failed");
    ESP_RETURN_ON_ERROR(board_7c_set_backlight(0), TAG, "Backlight off failed");
    ESP_RETURN_ON_ERROR(touch_init(&handles->touch), TAG, "Touch init failed");
    ESP_RETURN_ON_ERROR(lcd_init(&handles->panel), TAG, "LCD init failed");

    ESP_RETURN_ON_ERROR(board_7c_set_backlight(settings_get()->backlight_percent), TAG, "Backlight on failed");

    ESP_LOGI(TAG, "Waveshare panel ready: 800x480 RGB565 + GT911 (%s)",
             s_hw_profile == HW_PROFILE_7C_BOX ? "7C-BOX" : "ESP32-S3-Touch-LCD-7");
    return ESP_OK;
}

#endif
