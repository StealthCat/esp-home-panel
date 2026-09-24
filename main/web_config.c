#include "web_config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network.h"
#include "settings.h"

static const char *TAG = "web_config";
static httpd_handle_t s_server;

static const char PAGE_HEAD[] =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP Home Panel</title><style>"
":root{color-scheme:dark;background:#07111c;color:#f7fbff;font-family:system-ui,-apple-system,Segoe UI,sans-serif}"
"*{box-sizing:border-box}body{margin:0;background:linear-gradient(180deg,#07111c,#0b1d31);min-height:100vh}"
".wrap{max-width:880px;margin:auto;padding:24px}.top{display:flex;justify-content:space-between;gap:20px;align-items:center;margin-bottom:20px}"
"h1{font-size:28px;margin:0}.sub{color:#a7bcd0;margin-top:5px}.pill{background:#15334a;border:1px solid #35536d;border-radius:999px;padding:8px 12px;color:#bfe8ff}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px}"
".card{background:#101d2a;border:1px solid #35536d;border-radius:16px;padding:18px;box-shadow:0 10px 30px #0005}"
"h2{font-size:18px;margin:0 0 15px;color:#8ed5ff}label{display:block;font-size:13px;color:#a7bcd0;margin:12px 0 6px}"
"input,select{width:100%;padding:11px 12px;border-radius:9px;border:1px solid #35536d;background:#071722;color:#f7fbff;font-size:15px}"
"input[type=range]{padding:0}.row{display:flex;gap:10px;align-items:center}.row>*{flex:1}"
".check{display:flex;gap:8px;align-items:center;font-size:13px;color:#a7bcd0;margin-top:8px}.check input{width:auto}"
".hint{font-size:12px;color:#829bb0;margin-top:7px;line-height:1.4}.actions{display:flex;gap:12px;flex-wrap:wrap;margin-top:18px}"
"button{border:0;border-radius:10px;padding:12px 18px;font-size:15px;font-weight:700;cursor:pointer;background:#21a7ff;color:#03111c}"
"button.secondary{background:#24394b;color:#f7fbff}button.danger{background:#ff667d;color:#21030a}"
".full{grid-column:1/-1}.status{font-size:13px;line-height:1.6;color:#bdd0df}.status b{color:#fff}"
"@media(max-width:600px){.wrap{padding:14px}.top{align-items:flex-start;flex-direction:column}}"
"</style></head><body><div class='wrap'>";

static const char PAGE_FOOT[] =
"</div><script>"
"const r=document.getElementById('backlight'),v=document.getElementById('backlightValue');"
"if(r&&v){const u=()=>v.textContent=r.value+'%';r.addEventListener('input',u);u();}"
"</script></body></html>";

static void html_escape(const char *src, char *dst, size_t dst_size)
{
    size_t used = 0;
    if (!dst_size) return;

    for (; *src && used + 1 < dst_size; ++src) {
        const char *rep = NULL;
        switch (*src) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': rep = "&quot;"; break;
        case '\'': rep = "&#39;"; break;
        default:
            dst[used++] = *src;
            continue;
        }

        size_t n = strlen(rep);
        if (used + n >= dst_size) break;
        memcpy(dst + used, rep, n);
        used += n;
    }
    dst[used] = '\0';
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void url_decode(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    size_t out = 0;
    if (!dst_size) return;

    for (size_t i = 0; i < src_len && out + 1 < dst_size; ++i) {
        if (src[i] == '+') {
            dst[out++] = ' ';
        } else if (src[i] == '%' && i + 2 < src_len) {
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                dst[out++] = src[i];
            }
        } else {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
}

static bool form_get(const char *body, const char *key, char *out, size_t out_size)
{
    size_t key_len = strlen(key);
    const char *p = body;

    while (*p) {
        const char *pair_end = strchr(p, '&');
        if (!pair_end) pair_end = p + strlen(p);

        const char *eq = memchr(p, '=', (size_t)(pair_end - p));
        if (eq && (size_t)(eq - p) == key_len && memcmp(p, key, key_len) == 0) {
            url_decode(out, out_size, eq + 1, (size_t)(pair_end - eq - 1));
            return true;
        }

        p = *pair_end ? pair_end + 1 : pair_end;
    }

    if (out_size) out[0] = '\0';
    return false;
}

static bool form_has(const char *body, const char *key)
{
    char tmp[8];
    return form_get(body, key, tmp, sizeof(tmp));
}

static bool valid_panel_id(const char *value)
{
    if (!value || value[0] == '\0') {
        return false;
    }

    size_t len = strlen(value);
    if (len > PANEL_ID_MAX) {
        return false;
    }

    for (const char *p = value; *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_') {
            continue;
        }
        return false;
    }

    return true;
}

static bool valid_entity_syntax(const char *value)
{
    if (!value || value[0] == '\0') {
        return true;
    }

    const char *dot = strchr(value, '.');
    if (!dot || dot == value || dot[1] == '\0' || strchr(dot + 1, '.')) {
        return false;
    }

    for (const char *p = value; *p; ++p) {
        if (*p == '.') {
            continue;
        }
        if ((*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') ||
            *p == '_') {
            continue;
        }
        return false;
    }

    return true;
}

static bool entity_domain_is(const char *value, const char *domain)
{
    if (!value || value[0] == '\0') {
        return true;
    }

    size_t len = strlen(domain);
    return strncmp(value, domain, len) == 0 && value[len] == '.';
}

static bool valid_home_assistant_targets(const panel_settings_t *cfg)
{
    if (!valid_entity_syntax(cfg->ha_tapo_entity) ||
        !valid_entity_syntax(cfg->ha_overhead_entity) ||
        !valid_entity_syntax(cfg->ha_fan_entity) ||
        !valid_entity_syntax(cfg->ha_echo_entity) ||
        !valid_entity_syntax(cfg->ha_echo_announce_script)) {
        return false;
    }

    if (!entity_domain_is(cfg->ha_tapo_entity, "light") ||
        !entity_domain_is(cfg->ha_overhead_entity, "light") ||
        !entity_domain_is(cfg->ha_echo_entity, "media_player") ||
        !entity_domain_is(cfg->ha_echo_announce_script, "script")) {
        return false;
    }

    if (cfg->ha_fan_entity[0] != '\0' &&
        !entity_domain_is(cfg->ha_fan_entity, "fan") &&
        !entity_domain_is(cfg->ha_fan_entity, "switch")) {
        return false;
    }

    return true;
}

static esp_err_t send_chunk(httpd_req_t *req, const char *text)
{
    return httpd_resp_send_chunk(req, text, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t root_handler(httpd_req_t *req)
{
    const panel_settings_t *cfg = settings_get();

    char ssid[128], mqtt_uri[384], mqtt_user[160], mqtt_topic[320], panel_id[96], timezone[224];
    char ha_tapo[160], ha_overhead[160], ha_fan[160], ha_echo[160], ha_announce[160];
    html_escape(cfg->wifi_ssid, ssid, sizeof(ssid));
    html_escape(cfg->mqtt_uri, mqtt_uri, sizeof(mqtt_uri));
    html_escape(cfg->mqtt_username, mqtt_user, sizeof(mqtt_user));
    html_escape(cfg->mqtt_base_topic, mqtt_topic, sizeof(mqtt_topic));
    html_escape(cfg->panel_id, panel_id, sizeof(panel_id));
    html_escape(cfg->ha_tapo_entity, ha_tapo, sizeof(ha_tapo));
    html_escape(cfg->ha_overhead_entity, ha_overhead, sizeof(ha_overhead));
    html_escape(cfg->ha_fan_entity, ha_fan, sizeof(ha_fan));
    html_escape(cfg->ha_echo_entity, ha_echo, sizeof(ha_echo));
    html_escape(cfg->ha_echo_announce_script, ha_announce, sizeof(ha_announce));
    html_escape(cfg->timezone, timezone, sizeof(timezone));

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, PAGE_HEAD));

    char buf[2048];
    const char *ip = network_get_sta_ip();
    snprintf(buf, sizeof(buf),
             "<div class='top'><div><h1>ESP Home Panel</h1>"
             "<div class='sub'>Runtime configuration stored in NVS</div></div>"
             "<div class='pill'>%s</div></div>",
             network_is_connected() ? "Wi-Fi connected" : "Setup mode");
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    snprintf(buf, sizeof(buf),
             "<div class='card full status'><b>Network status</b><br>"
             "Station IP: %s<br>"
             "Setup AP: %s%s%s</div>",
             ip ? ip : "not connected",
             network_config_ap_active() ? network_get_config_ap_ssid() : "inactive",
             network_config_ap_active() ? " &nbsp; Password: " : "",
             network_config_ap_active() ? network_get_config_ap_password() : "");
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, "<form method='post' action='/save'><div class='grid'>"));

    snprintf(buf, sizeof(buf),
             "<section class='card'><h2>Wi-Fi</h2>"
             "<label>SSID</label><input name='wifi_ssid' maxlength='32' value=\"%s\" autocomplete='off'>"
             "<label>Password</label><input name='wifi_password' type='password' maxlength='64' placeholder='Leave blank to keep current password' autocomplete='new-password'>"
             "<label class='check'><input type='checkbox' name='clear_wifi_password' value='1'>Clear saved Wi-Fi password</label>"
             "<div class='hint'>If the panel cannot join this network after reboot, it starts a setup access point automatically.</div>"
             "</section>", ssid);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    snprintf(buf, sizeof(buf),
             "<section class='card'><h2>Display &amp; Theme</h2>"
             "<label>Theme</label><select name='theme'>"
             "<option value='0'%s>Classic dark</option>"
             "<option value='1'%s>Soccer green</option>"
             "<option value='2'%s>Kittens pink</option>"
             "<option value='3'%s>Magic / bricks blue</option>"
             "</select>"
             "<label>Backlight <span id='backlightValue'></span></label>"
             "<input id='backlight' name='backlight' type='range' min='0' max='100' value='%u'>"
             "<div class='hint'>The ESP32-S3-Touch-LCD-7 hardware has switch-only backlight control; non-zero values are treated as on. 7C-BOX supports PWM brightness.</div>"
             "<label>POSIX timezone</label><input name='timezone' maxlength='95' value=\"%s\">"
             "</section>",
             cfg->theme == 0 ? " selected" : "",
             cfg->theme == 1 ? " selected" : "",
             cfg->theme == 2 ? " selected" : "",
             cfg->theme == 3 ? " selected" : "",
             cfg->backlight_percent,
             timezone);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    snprintf(buf, sizeof(buf),
             "<section class='card full'><h2>MQTT</h2>"
             "<div class='row'><div><label>Broker URI</label><input name='mqtt_uri' maxlength='191' value=\"%s\" placeholder='mqtt://192.168.1.2'></div>"
             "<div><label>Base topic</label><input name='mqtt_base_topic' maxlength='127' value=\"%s\"></div></div>"
             "<div class='row'><div><label>Panel ID</label><input name='panel_id' maxlength='31' value=\"%s\" autocapitalize='none' spellcheck='false'></div>"
             "<div><label>Effective topic root</label><input value=\"%s/%s\" readonly></div></div>"
             "<div class='hint'>Each panel must have a unique ID. Fresh panels default to a stable ID derived from their MAC address, so multiple panels can share one broker and one base topic safely.</div>"
             "<div class='row'><div><label>Username</label><input name='mqtt_username' maxlength='63' value=\"%s\" autocomplete='off'></div>"
             "<div><label>Password</label><input name='mqtt_password' type='password' maxlength='127' placeholder='Leave blank to keep current password' autocomplete='new-password'></div></div>"
             "<label class='check'><input type='checkbox' name='clear_mqtt_password' value='1'>Clear saved MQTT password</label>"
             "</section>",
             mqtt_uri, mqtt_topic, panel_id, mqtt_topic, panel_id, mqtt_user);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "<section class='card full'><h2>Home Assistant targets</h2>"
        "<div class='hint'>Mosquitto transports MQTT messages but does not expose Home Assistant's entity registry. "
        "Enter the entity IDs shown in Home Assistant. The included MQTT bridge automation translates panel commands into Home Assistant actions.</div>"));

    snprintf(buf, sizeof(buf),
             "<div class='row'><div><label>Tapo LED strips</label>"
             "<input name='ha_tapo_entity' maxlength='127' value=\"%s\" placeholder='light.tapo_led_strips' autocapitalize='none' spellcheck='false'></div>"
             "<div><label>Overhead light</label>"
             "<input name='ha_overhead_entity' maxlength='127' value=\"%s\" placeholder='light.office_overhead' autocapitalize='none' spellcheck='false'></div></div>",
             ha_tapo, ha_overhead);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    snprintf(buf, sizeof(buf),
             "<div class='row'><div><label>Ceiling fan / switch</label>"
             "<input name='ha_fan_entity' maxlength='127' value=\"%s\" placeholder='fan.office_fan or switch.office_fan' autocapitalize='none' spellcheck='false'></div>"
             "<div><label>Echo / media player</label>"
             "<input name='ha_echo_entity' maxlength='127' value=\"%s\" placeholder='media_player.office_echo' autocapitalize='none' spellcheck='false'></div></div>",
             ha_fan, ha_echo);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    snprintf(buf, sizeof(buf),
             "<label>Echo announcement script (optional)</label>"
             "<input name='ha_echo_announce_script' maxlength='127' value=\"%s\" placeholder='script.office_echo_announce' autocapitalize='none' spellcheck='false'>",
             ha_announce);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "<div class='hint'>Leave a field blank to leave that control unmapped. Light controls require light.*, Echo requires media_player.*, "
        "fan accepts fan.* or switch.*, and the optional announcement action requires script.*.</div>"
        "</section>"));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "</div><div class='actions'><button type='submit'>Save &amp; reboot</button>"
        "<button class='secondary' type='button' onclick=\"location.reload()\">Reload</button>"
        "</div></form>"
        "<form method='post' action='/reset' onsubmit=\"return confirm('Reset all web configuration to firmware defaults?')\">"
        "<div class='actions'><button class='danger' type='submit'>Reset configuration</button></div></form>"));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, PAGE_FOOT));
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t read_request_body(httpd_req_t *req, char **out_body)
{
    if (req->content_len <= 0 || req->content_len > 4096) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *body = malloc(req->content_len + 1);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            free(body);
            return ESP_FAIL;
        }
        received += (size_t)n;
    }

    body[received] = '\0';
    *out_body = body;
    return ESP_OK;
}

static esp_err_t save_handler(httpd_req_t *req)
{
    char *body = NULL;
    esp_err_t err = read_request_body(req, &body);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form body");
        return ESP_OK;
    }

    panel_settings_t updated = *settings_get();
    char tmp[256];

    if (form_get(body, "wifi_ssid", updated.wifi_ssid, sizeof(updated.wifi_ssid))) {
        /* copied */
    }

    if (form_has(body, "clear_wifi_password")) {
        updated.wifi_password[0] = '\0';
    } else if (form_get(body, "wifi_password", tmp, sizeof(tmp)) && tmp[0]) {
        strlcpy(updated.wifi_password, tmp, sizeof(updated.wifi_password));
    }

    form_get(body, "mqtt_uri", updated.mqtt_uri, sizeof(updated.mqtt_uri));
    form_get(body, "mqtt_username", updated.mqtt_username, sizeof(updated.mqtt_username));
    form_get(body, "mqtt_base_topic", updated.mqtt_base_topic, sizeof(updated.mqtt_base_topic));
    form_get(body, "panel_id", updated.panel_id, sizeof(updated.panel_id));
    form_get(body, "ha_tapo_entity", updated.ha_tapo_entity, sizeof(updated.ha_tapo_entity));
    form_get(body, "ha_overhead_entity", updated.ha_overhead_entity, sizeof(updated.ha_overhead_entity));
    form_get(body, "ha_fan_entity", updated.ha_fan_entity, sizeof(updated.ha_fan_entity));
    form_get(body, "ha_echo_entity", updated.ha_echo_entity, sizeof(updated.ha_echo_entity));
    form_get(body, "ha_echo_announce_script", updated.ha_echo_announce_script,
             sizeof(updated.ha_echo_announce_script));
    form_get(body, "timezone", updated.timezone, sizeof(updated.timezone));

    if (form_has(body, "clear_mqtt_password")) {
        updated.mqtt_password[0] = '\0';
    } else if (form_get(body, "mqtt_password", tmp, sizeof(tmp)) && tmp[0]) {
        strlcpy(updated.mqtt_password, tmp, sizeof(updated.mqtt_password));
    }

    if (form_get(body, "theme", tmp, sizeof(tmp))) {
        long theme = strtol(tmp, NULL, 10);
        if (theme >= 0 && theme <= 3) {
            updated.theme = (uint8_t)theme;
        }
    }

    if (form_get(body, "backlight", tmp, sizeof(tmp))) {
        long backlight = strtol(tmp, NULL, 10);
        if (backlight >= 0 && backlight <= 100) {
            updated.backlight_percent = (uint8_t)backlight;
        }
    }

    if (!valid_panel_id(updated.panel_id)) {
        free(body);
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid panel ID. Use 1-31 lowercase letters, digits, '-' or '_'.");
        return ESP_OK;
    }

    if (!valid_home_assistant_targets(&updated)) {
        free(body);
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid Home Assistant entity ID or entity domain");
        return ESP_OK;
    }

    free(body);

    if (updated.mqtt_base_topic[0] == '\0') {
        strlcpy(updated.mqtt_base_topic, "home/7c-panel", sizeof(updated.mqtt_base_topic));
    }
    if (updated.timezone[0] == '\0') {
        strlcpy(updated.timezone, "EST5EDT,M3.2.0/2,M11.1.0/2", sizeof(updated.timezone));
    }

    err = settings_save(&updated);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save web configuration: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save configuration");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Configuration saved; rebooting to apply settings");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<body style='background:#07111c;color:#fff;font-family:system-ui;padding:30px'>"
        "<h2>Configuration saved</h2><p>The panel is rebooting now. Reconnect in a few seconds.</p></body>");

    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
    return ESP_OK;
}

static esp_err_t reset_handler(httpd_req_t *req)
{
    esp_err_t err = settings_reset();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to reset configuration");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Saved configuration reset to firmware defaults; rebooting");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!doctype html><body style='background:#07111c;color:#fff;font-family:system-ui;padding:30px'>"
        "<h2>Configuration reset</h2><p>The panel is rebooting with firmware defaults.</p></body>");
    vTaskDelay(pdMS_TO_TICKS(700));
    esp_restart();
    return ESP_OK;
}

esp_err_t web_config_start(void)
{
    if (s_server) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
    };
    const httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_handler,
    };
    const httpd_uri_t reset = {
        .uri = "/reset",
        .method = HTTP_POST,
        .handler = reset_handler,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &save));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &reset));

    if (network_is_connected() && network_get_sta_ip()) {
        ESP_LOGI(TAG, "Configuration UI: http://%s/", network_get_sta_ip());
    }
    if (network_config_ap_active()) {
        ESP_LOGI(TAG, "Configuration UI: http://192.168.4.1/");
    }

    return ESP_OK;
}
