#pragma once
#include <stdbool.h>
#include "esp_http_server.h"

void web_security_init(void);
bool web_security_authorize(httpd_req_t *req, bool password_required);
const char *web_security_token(void);
bool web_security_check_token(const char *token);
