#pragma once
#include <stdbool.h>
#include "esp_http_server.h"

/* Arm first-boot recovery before initializing hardware/networking. */
void ota_update_boot_begin(void);
void ota_update_boot_poll(bool local_services_ready);
esp_err_t ota_update_register(httpd_handle_t server);
