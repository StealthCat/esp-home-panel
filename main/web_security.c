#include "web_security.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "mbedtls/base64.h"

static char s_token[33];

void web_security_init(void)
{
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    for (size_t i = 0; i < sizeof(random); ++i) {
        snprintf(s_token + i * 2, 3, "%02x", random[i]);
    }
}

const char *web_security_token(void) { return s_token; }

static bool equal_secret(const char *a, const char *b)
{
    size_t length = strlen(a);
    if (length != strlen(b)) return false;
    volatile unsigned difference = 0;
    for (size_t i = 0; i < length; ++i) difference |= (unsigned char)a[i] ^ (unsigned char)b[i];
    return difference == 0;
}

bool web_security_check_token(const char *token)
{
    return s_token[0] && token && equal_secret(s_token, token);
}

bool web_security_authorize(httpd_req_t *req, bool password_required)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    const char *password = settings_get()->admin_password;
    if (!password[0]) {
        if (!password_required) return true; /* Initial provisioning. */
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Set an administrator password in Settings before uploading firmware.");
        return false;
    }

    char credentials[PANEL_ADMIN_PASSWORD_MAX + 7];
    char expected[128] = "Basic ", supplied[128];
    snprintf(credentials, sizeof(credentials), "admin:%s", password);
    size_t length = 0;
    int result = mbedtls_base64_encode((unsigned char *)expected + 6, sizeof(expected) - 7,
                                     &length, (unsigned char *)credentials, strlen(credentials));
    expected[6 + length] = '\0';
    memset(credentials, 0, sizeof(credentials));
    if (result == 0 && httpd_req_get_hdr_value_str(req, "Authorization", supplied, sizeof(supplied)) == ESP_OK &&
        equal_secret(expected, supplied)) return true;

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP Home Panel\"");
    httpd_resp_sendstr(req, "Sign in with username admin and your administrator password.");
    return false;
}
