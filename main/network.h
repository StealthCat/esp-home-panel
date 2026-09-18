#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t network_start(void);
bool network_is_connected(void);

#ifdef __cplusplus
}
#endif
