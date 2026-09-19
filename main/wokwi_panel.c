#include "wokwi_panel.h"

#ifdef WOKWI_EMULATOR

#include <string.h>

#include "board_7c.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "wokwi_panel";

#define WOKWI_SPI_HOST          SPI2_HOST
#define WOKWI_SPI_SCK           GPIO_NUM_12
#define WOKWI_SPI_MOSI          GPIO_NUM_11
#define WOKWI_SPI_CS            GPIO_NUM_10
#define WOKWI_SPI_HZ            (20 * 1000 * 1000)

#define WOKWI_I2C_PORT          I2C_NUM_0
#define WOKWI_I2C_SDA           GPIO_NUM_9
#define WOKWI_I2C_SCL           GPIO_NUM_8
#define WOKWI_TOUCH_ADDR        0x38

#define PACKET_MAGIC            0x7c
#define PACKET_HEADER_BYTES     13
#define PACKET_PIXELS           2000
#define DRAW_ROWS               48
#define DRAW_BUFFER_BYTES       (PANEL_H_RES * DRAW_ROWS * 2)

static spi_device_handle_t s_spi;
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_touch;
static void *s_draw_buffer;
static DMA_ATTR uint8_t s_packet[PACKET_HEADER_BYTES + PACKET_PIXELS * 2];

static inline void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xff);
    p[1] = (uint8_t)(value >> 8);
}

static inline void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xff);
    p[1] = (uint8_t)((value >> 8) & 0xff);
    p[2] = (uint8_t)((value >> 16) & 0xff);
    p[3] = (uint8_t)((value >> 24) & 0xff);
}

static void wokwi_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    const uint16_t width = (uint16_t)(area->x2 - area->x1 + 1);
    const uint16_t height = (uint16_t)(area->y2 - area->y1 + 1);
    const uint32_t total_pixels = (uint32_t)width * height;
    uint32_t offset = 0;

    while (offset < total_pixels) {
        uint16_t count = (uint16_t)(total_pixels - offset);
        if (count > PACKET_PIXELS) count = PACKET_PIXELS;

        s_packet[0] = PACKET_MAGIC;
        put_u16(s_packet + 1, (uint16_t)area->x1);
        put_u16(s_packet + 3, (uint16_t)area->y1);
        put_u16(s_packet + 5, width);
        put_u32(s_packet + 7, offset);
        put_u16(s_packet + 11, count);
        memcpy(s_packet + PACKET_HEADER_BYTES, px_map + offset * 2, count * 2);

        spi_transaction_t tx = {
            .length = (PACKET_HEADER_BYTES + count * 2) * 8,
            .tx_buffer = s_packet,
        };
        esp_err_t err = spi_device_transmit(s_spi, &tx);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Virtual display SPI transfer failed: %s", esp_err_to_name(err));
            break;
        }
        offset += count;
    }

    lv_display_flush_ready(display);
}

static void wokwi_touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint8_t raw[5] = {0};

    if (s_touch && i2c_master_receive(s_touch, raw, sizeof(raw), 5) == ESP_OK) {
        const uint16_t x = (uint16_t)raw[1] | ((uint16_t)raw[2] << 8);
        const uint16_t y = (uint16_t)raw[3] | ((uint16_t)raw[4] << 8);
        data->point.x = x < PANEL_H_RES ? x : PANEL_H_RES - 1;
        data->point.y = y < PANEL_V_RES ? y : PANEL_V_RES - 1;
        data->state = raw[0] ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

esp_err_t wokwi_panel_init(void)
{
    const spi_bus_config_t spi_bus = {
        .mosi_io_num = WOKWI_SPI_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = WOKWI_SPI_SCK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = sizeof(s_packet),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(WOKWI_SPI_HOST, &spi_bus, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus init failed");

    const spi_device_interface_config_t spi_device = {
        .clock_speed_hz = WOKWI_SPI_HZ,
        .mode = 0,
        .spics_io_num = WOKWI_SPI_CS,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(WOKWI_SPI_HOST, &spi_device, &s_spi),
                        TAG, "SPI display device init failed");

    const i2c_master_bus_config_t i2c_bus = {
        .i2c_port = WOKWI_I2C_PORT,
        .sda_io_num = WOKWI_I2C_SDA,
        .scl_io_num = WOKWI_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus, &s_i2c_bus), TAG, "Touch I2C bus init failed");

    const i2c_device_config_t touch_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = WOKWI_TOUCH_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &touch_cfg, &s_touch),
                        TAG, "Virtual touch device init failed");

    return ESP_OK;
}

lv_display_t *wokwi_panel_create_display(void)
{
    lv_display_t *display = lv_display_create(PANEL_H_RES, PANEL_V_RES);
    if (!display) return NULL;

    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, wokwi_flush);

    s_draw_buffer = heap_caps_aligned_alloc(64, DRAW_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_draw_buffer) {
        s_draw_buffer = heap_caps_aligned_alloc(64, DRAW_BUFFER_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_draw_buffer) {
        ESP_LOGE(TAG, "Unable to allocate %u-byte LVGL Wokwi draw buffer", (unsigned)DRAW_BUFFER_BYTES);
        lv_display_delete(display);
        return NULL;
    }

    lv_display_set_buffers(display, s_draw_buffer, NULL, DRAW_BUFFER_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_default(display);
    return display;
}

esp_err_t wokwi_panel_add_touch(lv_display_t *display)
{
    lv_indev_t *indev = lv_indev_create();
    ESP_RETURN_ON_FALSE(indev, ESP_ERR_NO_MEM, TAG, "Unable to create LVGL touch input");
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, display);
    lv_indev_set_read_cb(indev, wokwi_touch_read);
    return ESP_OK;
}

#endif
