#include "wokwi-api.h"

#define PANEL_W 800u
#define PANEL_H 480u
#define SPI_BUFFER_SIZE 4096u
#define MAX_PACKET_PIXELS 2000u
#define PACKET_HEADER 13u
#define TOUCH_ADDR 0x38u

typedef struct {
  pin_t cs;
  spi_dev_t spi;
  buffer_t framebuffer;
  uint32_t fb_width;
  uint32_t fb_height;
  uint32_t touch_x_attr;
  uint32_t touch_y_attr;
  uint32_t touch_pressed_attr;
  uint8_t touch_reply[5];
  uint8_t touch_reply_index;
} chip_state_t;

static chip_state_t chip;
static uint8_t spi_buffer[SPI_BUFFER_SIZE];
static uint8_t rgba_buffer[MAX_PACKET_PIXELS * 4u];

static uint16_t u16le(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32le(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static void process_packet(uint8_t *buffer, uint32_t count) {
  if (count < PACKET_HEADER || buffer[0] != 0x7c) return;
  const uint16_t x0 = u16le(buffer + 1);
  const uint16_t y0 = u16le(buffer + 3);
  const uint16_t width = u16le(buffer + 5);
  const uint32_t pixel_offset = u32le(buffer + 7);
  const uint16_t pixels = u16le(buffer + 11);
  if (width == 0 || pixels == 0 || pixels > MAX_PACKET_PIXELS) return;
  if (PACKET_HEADER + (uint32_t)pixels * 2u > count) return;

  const uint8_t *src = buffer + PACKET_HEADER;
  uint32_t done = 0;
  while (done < pixels) {
    const uint32_t linear = pixel_offset + done;
    const uint32_t row = linear / width;
    const uint32_t col = linear % width;
    if ((uint32_t)y0 + row >= chip.fb_height || (uint32_t)x0 + col >= chip.fb_width) return;

    uint32_t run = pixels - done;
    const uint32_t row_room = width - col;
    if (run > row_room) run = row_room;
    if ((uint32_t)x0 + col + run > chip.fb_width) run = chip.fb_width - ((uint32_t)x0 + col);

    for (uint32_t i = 0; i < run; ++i) {
      const uint32_t si = (done + i) * 2u;
      const uint16_t rgb565 = (uint16_t)src[si] | ((uint16_t)src[si + 1] << 8);
      const uint8_t r5 = (rgb565 >> 11) & 0x1f;
      const uint8_t g6 = (rgb565 >> 5) & 0x3f;
      const uint8_t b5 = rgb565 & 0x1f;
      const uint32_t di = i * 4u;
      rgba_buffer[di + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
      rgba_buffer[di + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
      rgba_buffer[di + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
      rgba_buffer[di + 3] = 0xff;
    }

    const uint32_t fb_pixel = (((uint32_t)y0 + row) * chip.fb_width) + (uint32_t)x0 + col;
    buffer_write(chip.framebuffer, fb_pixel * 4u, rgba_buffer, run * 4u);
    done += run;
  }
}

static void spi_done(void *user_data, uint8_t *buffer, uint32_t count) {
  (void)user_data;
  if (count) process_packet(buffer, count);
  if (pin_read(chip.cs) == LOW) spi_start(chip.spi, spi_buffer, sizeof(spi_buffer));
}

static void cs_changed(void *user_data, pin_t pin, uint32_t value) {
  (void)user_data; (void)pin;
  if (value == LOW) spi_start(chip.spi, spi_buffer, sizeof(spi_buffer));
  else spi_stop(chip.spi);
}

static bool touch_connect(void *user_data, uint32_t address, bool read) {
  (void)user_data;
  if (address != TOUCH_ADDR) return false;
  chip.touch_reply_index = 0;
  if (read) {
    uint32_t x = attr_read(chip.touch_x_attr);
    uint32_t y = attr_read(chip.touch_y_attr);
    uint32_t pressed = attr_read(chip.touch_pressed_attr);
    if (x >= PANEL_W) x = PANEL_W - 1;
    if (y >= PANEL_H) y = PANEL_H - 1;
    chip.touch_reply[0] = pressed ? 1 : 0;
    chip.touch_reply[1] = (uint8_t)(x & 0xff);
    chip.touch_reply[2] = (uint8_t)((x >> 8) & 0xff);
    chip.touch_reply[3] = (uint8_t)(y & 0xff);
    chip.touch_reply[4] = (uint8_t)((y >> 8) & 0xff);
  }
  return true;
}
static uint8_t touch_read(void *user_data) {
  (void)user_data;
  if (chip.touch_reply_index < sizeof(chip.touch_reply)) return chip.touch_reply[chip.touch_reply_index++];
  return 0;
}
static bool touch_write(void *user_data, uint8_t data) { (void)user_data; (void)data; return true; }

void chip_init(void) {
  chip.framebuffer = framebuffer_init(&chip.fb_width, &chip.fb_height);
  chip.cs = pin_init("CS", INPUT_PULLUP);
  pin_t sck = pin_init("SCK", INPUT);
  pin_t mosi = pin_init("MOSI", INPUT);
  pin_t scl = pin_init("SCL", INPUT_PULLUP);
  pin_t sda = pin_init("SDA", INPUT_PULLUP);
  chip.touch_x_attr = attr_init("touchX", 400);
  chip.touch_y_attr = attr_init("touchY", 240);
  chip.touch_pressed_attr = attr_init("touchPressed", 0);

  const spi_config_t spi_cfg = {.user_data=&chip,.sck=sck,.mosi=mosi,.miso=NO_PIN,.mode=0,.done=spi_done};
  chip.spi = spi_init(&spi_cfg);
  const pin_watch_config_t cs_watch = {.user_data=&chip,.edge=BOTH,.pin_change=cs_changed};
  pin_watch(chip.cs, &cs_watch);

  const i2c_config_t i2c_cfg = {
    .user_data=&chip,.address=TOUCH_ADDR,.scl=scl,.sda=sda,
    .connect=touch_connect,.read=touch_read,.write=touch_write,.disconnect=0
  };
  i2c_init(&i2c_cfg);
}
