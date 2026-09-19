#include <stdint.h>
#include <stdbool.h>
#ifndef WOKWI_API_H
#define WOKWI_API_H
enum pin_value { LOW=0, HIGH=1 };
enum pin_mode { INPUT=0, OUTPUT=1, INPUT_PULLUP=2, INPUT_PULLDOWN=3, ANALOG=4, OUTPUT_LOW=16, OUTPUT_HIGH=17 };
enum edge { RISING=1, FALLING=2, BOTH=3 };
int __attribute__((export_name("__wokwi_api_version_1"))) __attribute__((weak)) __wokwi_api_version_1(void) { return 1; }
typedef int32_t pin_t;
#define NO_PIN ((pin_t)-1)
typedef struct { void *user_data; uint32_t edge; void (*pin_change)(void*,pin_t,uint32_t); } pin_watch_config_t;
extern __attribute__((export_name("chipInit"))) void chip_init(void);
extern __attribute__((import_name("pinInit"))) pin_t pin_init(const char*,uint32_t);
extern __attribute__((import_name("pinRead"))) uint32_t pin_read(pin_t);
extern __attribute__((import_name("pinWatch"))) bool pin_watch(pin_t,const pin_watch_config_t*);
extern __attribute__((import_name("attrInit"))) uint32_t attr_init(const char*,uint32_t);
extern __attribute__((import_name("attrRead"))) uint32_t attr_read(uint32_t);
typedef struct { void *user_data; uint32_t address; pin_t scl; pin_t sda; bool (*connect)(void*,uint32_t,bool); uint8_t (*read)(void*); bool (*write)(void*,uint8_t); void (*disconnect)(void*); uint32_t reserved[8]; } i2c_config_t;
typedef uint32_t i2c_dev_t;
extern __attribute__((import_name("i2cInit"))) i2c_dev_t i2c_init(const i2c_config_t*);
typedef struct { void *user_data; pin_t sck; pin_t mosi; pin_t miso; uint32_t mode; void (*done)(void*,uint8_t*,uint32_t); uint32_t reserved[8]; } spi_config_t;
typedef uint32_t spi_dev_t;
extern __attribute__((import_name("spiInit"))) spi_dev_t spi_init(const spi_config_t*);
extern __attribute__((import_name("spiStart"))) void spi_start(spi_dev_t,uint8_t*,uint32_t);
extern __attribute__((import_name("spiStop"))) void spi_stop(spi_dev_t);
typedef uint32_t buffer_t;
extern __attribute__((import_name("framebufferInit"))) buffer_t framebuffer_init(uint32_t*,uint32_t*);
extern __attribute__((import_name("bufferWrite"))) void buffer_write(buffer_t,uint32_t,uint8_t*,uint32_t);
#endif
