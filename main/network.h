#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t network_start(void);
bool network_is_connected(void);
bool network_config_ap_active(void);
const char *network_get_sta_ip(void);
const char *network_get_config_ap_ssid(void);
const char *network_get_config_ap_password(void);

#ifdef __cplusplus
}
#endif
