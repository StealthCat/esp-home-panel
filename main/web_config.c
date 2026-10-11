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
#include "network_identity.h"
#include "settings.h"
#include "web_security.h"
#include "ota_update.h"

static const char *TAG = "web_config";
static httpd_handle_t s_server;

#include "web_config_assets.h"

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

/* Accept only a complete #RRGGBB value, including black (zero). */
static bool parse_preset_color(const char *text, uint32_t *rgb)
{
    if (strlen(text) != 7 || text[0] != '#') return false;
    uint32_t value = 0;
    for (int i = 1; i < 7; ++i) {
        int digit = hex_value(text[i]);
        if (digit < 0) return false;
        value = (value << 4) | (uint32_t)digit;
    }
    *rgb = value;
    return true;
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
            if (hi >= 0 && lo >= 0 && (hi != 0 || lo != 0)) {
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

static bool valid_display_title(const char *value)
{
    if (!value || value[0] == '\0') {
        return false;
    }

    size_t len = strlen(value);
    if (len > PANEL_DISPLAY_TITLE_MAX) {
        return false;
    }

    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p < 0x20 || *p == 0x7F) {
            return false;
        }
    }

    return true;
}

static bool valid_mqtt_prefix(const char *value)
{
    if (!value || value[0] == '\0') {
        return false;
    }

    for (const char *p = value; *p; ++p) {
        if (*p == '+' || *p == '#') {
            return false;
        }
        if ((unsigned char)*p < 0x20) {
            return false;
        }
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
        !valid_entity_syntax(cfg->ha_echo_entity)) {
        return false;
    }

    if (!entity_domain_is(cfg->ha_tapo_entity, "light") ||
        !entity_domain_is(cfg->ha_overhead_entity, "light") ||
        !entity_domain_is(cfg->ha_echo_entity, "media_player")) {
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
    if (!web_security_authorize(req, false)) return ESP_OK;
    const panel_settings_t *cfg = settings_get();

    char ssid[128], mqtt_uri[384], mqtt_user[160], mqtt_topic[320], mqtt_discovery[160], panel_id[96], timezone[224];
    char display_title[128];
    char ha_tapo[160], ha_overhead[160], ha_fan[160], ha_echo[160];
    html_escape(cfg->wifi_ssid, ssid, sizeof(ssid));
    html_escape(cfg->mqtt_uri, mqtt_uri, sizeof(mqtt_uri));
    html_escape(cfg->mqtt_username, mqtt_user, sizeof(mqtt_user));
    html_escape(cfg->mqtt_base_topic, mqtt_topic, sizeof(mqtt_topic));
    html_escape(cfg->mqtt_discovery_prefix, mqtt_discovery, sizeof(mqtt_discovery));
    html_escape(cfg->panel_id, panel_id, sizeof(panel_id));
    html_escape(cfg->display_title, display_title, sizeof(display_title));
    html_escape(cfg->ha_tapo_entity, ha_tapo, sizeof(ha_tapo));
    html_escape(cfg->ha_overhead_entity, ha_overhead, sizeof(ha_overhead));
    html_escape(cfg->ha_fan_entity, ha_fan, sizeof(ha_fan));
    html_escape(cfg->ha_echo_entity, ha_echo, sizeof(ha_echo));
    html_escape(cfg->timezone, timezone, sizeof(timezone));

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, PAGE_HEAD));

    char buf[2048];
    const char *ip = network_get_sta_ip();
    snprintf(buf, sizeof(buf),
             "<div class='top'><div class='brand'><span class='brandmark' aria-hidden='true'>&#9638;</span><div>ESP Home Panel"
             "<div class='eyebrow'>Device settings</div></div></div>"
             "<div class='pill'>%s</div></div>",
             network_is_connected() ? "Wi-Fi connected" : "Setup mode");
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, "<div class='intro'><div class='eyebrow'>Configuration</div><h1>Panel settings</h1><p class='sub'>Connection, display, sensors, and Home Assistant.</p></div>"));

    snprintf(buf, sizeof(buf),
             "<div class='status'>"
             "<span>IP address <b>%s</b></span>"
             "<span>Setup access point <b>%s</b>%s%s</span></div>",
             ip ? ip : "not connected",
             network_config_ap_active() ? network_get_config_ap_ssid() : "inactive",
             network_config_ap_active() ? " &nbsp; Password: " : "",
             network_config_ap_active() ? network_get_config_ap_password() : "");
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    snprintf(buf, sizeof(buf), "<nav id='settings-tabs' class='tabs' role='tablist' aria-label='Settings sections' hidden></nav><div id='save-notice' class='notice' role='status' aria-live='polite' hidden></div>"
             "<form id='settings-form' method='post' action='/save'><input type='hidden' name='csrf' value='%s'><div id='settings-grid' class='grid'>", web_security_token());
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "<section class='card' data-tab='system'><h2>Administrator access</h2>"
        "<label>New administrator password</label><input name='admin_password' type='password' minlength='8' maxlength='64' autocomplete='new-password' placeholder='Leave blank to keep current password'>"
        "<div class='hint'>Required for firmware uploads. Once set, sign in as <b>admin</b> to access settings and updates. Use 8&ndash;64 printable ASCII characters. Use this HTTP interface only on your trusted local network.</div></section>"));

    snprintf(buf, sizeof(buf),
             "<section class='card' data-tab='network'><h2>Wi-Fi &amp; Network</h2>"
             "<label>Hostname</label><input name='hostname' maxlength='32' required pattern='[a-z0-9](([a-z0-9]|-){0,30}[a-z0-9])?' value=\"%s\" autocapitalize='none' spellcheck='false'>"
             "<div class='hint'>Use a unique name without a domain, such as bedroom-panel. Advertised through DHCP and mDNS; independent of the MQTT panel ID.</div>"
             "<label>SSID</label><input name='wifi_ssid' maxlength='32' value=\"%s\" autocomplete='off'>"
             "<label>Password</label><input name='wifi_password' type='password' maxlength='64' placeholder='Leave blank to keep current password' autocomplete='new-password'>"
             "<label class='check'><input type='checkbox' name='clear_wifi_password' value='1'>Clear saved Wi-Fi password</label>"
             "<div class='hint'>If the panel cannot join this network after reboot, it starts a setup access point automatically.</div>"
             "</section>", cfg->hostname, ssid);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    char domain[PANEL_DOMAIN_MAX + 1];
    network_identity_get_domain(domain, sizeof(domain));
    snprintf(buf, sizeof(buf),
             "<section class='card' data-tab='network'><h2>Current network names</h2>"
             "<p>mDNS: <b>%s.local</b></p>"
             "<p>DHCP domain: <b>%s</b></p>"
             "<p>LAN name: <b>%s%s%s</b></p>"
             "<div class='hint'>LAN DNS records are managed by your router/DNS server. UniFi may need a lease refresh to show a changed hostname. The DHCP domain does not replace the .local mDNS domain.</div></section>",
             cfg->hostname, domain[0] ? domain : "Not supplied",
             cfg->hostname, domain[0] ? "." : "", domain);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    snprintf(buf, sizeof(buf),
             "<section class='card' data-tab='display'><h2>Display &amp; Theme</h2>"
             "<label>Display title</label><input name='display_title' required maxlength='31' value=\"%s\" placeholder='Home Control'>"
             "<div class='hint'>Shown as the large title in the upper-left of the panel.</div>"
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
             display_title,
             cfg->theme == 0 ? " selected" : "",
             cfg->theme == 1 ? " selected" : "",
             cfg->theme == 2 ? " selected" : "",
             cfg->theme == 3 ? " selected" : "",
             cfg->backlight_percent,
             timezone);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    snprintf(buf, sizeof(buf),
             "<section class='card' data-tab='sensors'><h2>Ambient light</h2>"
             "<label>Ambient light sensor</label><select name='ambient_light_enabled'>"
             "<option value='0'%s>Disabled</option><option value='1'%s>VEML7700 (I2C 0x10)</option></select>"
             "<label>Reporting interval (seconds)</label><input name='sensor_interval_seconds' type='number' min='1' max='60' required value='%u'>"
             "<div class='hint'>Connect a 3.3 V compatible VEML7700 to the shared I2C bus. Home Assistant automatically discovers Ambient light under this panel. Missing sensors report unavailable. Save and reboot to apply.</div></section>",
             cfg->ambient_light_enabled ? "" : " selected",
             cfg->ambient_light_enabled ? " selected" : "", cfg->sensor_interval_seconds);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    snprintf(buf, sizeof(buf),
             "<section class='card' data-tab='sensors'><h2>Presence sensor</h2>"
             "<label>Sensor</label><select name='presence_enabled'>"
             "<option value='0'%s>Disabled</option><option value='1'%s>Gravity C4001 (SEN0610, I2C)</option></select>"
             "<label>I2C address (match hardware switch)</label><select name='presence_address'>"
             "<option value='42'%s>0x2A</option><option value='43'%s>0x2B</option></select>"
             "<label>Maximum detection range (meters)</label><input type='number' name='presence_max_m' min='3' max='8' required value='%u'>"
             "<label>Trigger sensitivity (0-9)</label><input type='number' name='presence_trigger_sensitivity' min='0' max='9' required value='%u'>"
             "<label>Hold sensitivity (0-9)</label><input type='number' name='presence_keep_sensitivity' min='0' max='9' required value='%u'>"
             "<label>Vacancy delay (seconds)</label><input type='number' name='presence_hold_seconds' min='2' max='120' required value='%u'>"
             "<div class='hint'>Set the sensor hardware to I2C. Reports Occupancy through Home Assistant MQTT discovery, independently of lux reporting. Range starts at 0.3 m; practical coverage depends on mounting. Save and reboot to apply.</div></section>",
             cfg->presence_enabled ? "" : " selected", cfg->presence_enabled ? " selected" : "",
             cfg->presence_address == 42 ? " selected" : "", cfg->presence_address == 43 ? " selected" : "",
             cfg->presence_max_m, cfg->presence_trigger_sensitivity, cfg->presence_keep_sensitivity,
             cfg->presence_hold_seconds);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "<section class='card' data-tab='display'><h2>Tapo color circles</h2>"
        "<div class='hint'>Choose the six preset colors, ordered left to right on the display. "
        "These colors apply to every theme after Save &amp; reboot.</div><div class='presets'>"));
    for (int i = 0; i < PANEL_COLOR_PRESET_COUNT; ++i) {
        snprintf(buf, sizeof(buf),
                 "<div><label for='color_%d'>Circle %d</label>"
                 "<input id='color_%d' name='color_%d' type='color' value='#%06lx'></div>",
                 i, i + 1, i, i, (unsigned long)cfg->color_presets[i]);
        ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, "</div></section>"));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "<section class='card full' data-tab='home-assistant'><h2>MQTT</h2>"));

    snprintf(buf, sizeof(buf),
             "<div class='row'><div><label>Broker URI</label><input name='mqtt_uri' maxlength='191' value=\"%s\" placeholder='mqtt://192.168.1.2'></div>"
             "<div><label>Base topic</label><input name='mqtt_base_topic' maxlength='127' value=\"%s\"></div></div>",
             mqtt_uri, mqtt_topic);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    snprintf(buf, sizeof(buf),
             "<div class='row'><div><label>Panel ID</label><input name='panel_id' required pattern='[a-z0-9_-]+' maxlength='31' value=\"%s\" autocapitalize='none' spellcheck='false'></div>"
             "<div><label>Effective topic root</label><input id='topic-preview' value=\"%s/%s\" readonly></div></div>",
             panel_id, mqtt_topic, panel_id);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "<div class='hint'>Each panel must have a unique ID. Fresh panels default to a stable ID derived from their MAC address, so multiple panels can share one broker and one base topic safely.</div>"));

    snprintf(buf, sizeof(buf),
             "<div class='row'><div><label>Home Assistant discovery prefix</label>"
             "<input name='mqtt_discovery_prefix' required maxlength='63' value=\"%s\" autocapitalize='none' spellcheck='false'></div>"
             "<div><label>Discovery status topic</label><input id='discovery-preview' value=\"%s/status\" readonly></div></div>",
             mqtt_discovery, mqtt_discovery);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "<div class='hint'>Home Assistant's default MQTT discovery prefix is <b>homeassistant</b>. "
        "This is separate from the panel runtime base topic <b>home/7c-panel</b>.</div>"));

    snprintf(buf, sizeof(buf),
             "<div class='row'><div><label>Username</label><input name='mqtt_username' maxlength='63' value=\"%s\" autocomplete='off'></div>"
             "<div><label>Password</label><input name='mqtt_password' type='password' maxlength='127' placeholder='Leave blank to keep current password' autocomplete='new-password'></div></div>",
             mqtt_user);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "<label class='check'><input type='checkbox' name='clear_mqtt_password' value='1'>Clear saved MQTT password</label>"
        "</section>"));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "<section class='card full' data-tab='home-assistant'><h2>Home Assistant targets</h2>"
        "<div class='hint'>Mosquitto transports MQTT messages but does not expose Home Assistant's entity registry. "
        "Enter the entity IDs shown in Home Assistant. The included MQTT bridge automation translates panel commands into Home Assistant actions.</div>"));

    snprintf(buf, sizeof(buf),
             "<div class='row'><div><label>Light Strips</label>"
             "<input name='ha_tapo_entity' maxlength='127' value=\"%s\" placeholder='light.tapo_led_strips' autocapitalize='none' spellcheck='false'></div>"
             "<div><label>Main Lights</label>"
             "<input name='ha_overhead_entity' maxlength='127' value=\"%s\" placeholder='light.office_overhead' autocapitalize='none' spellcheck='false'></div></div>",
             ha_tapo, ha_overhead);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    snprintf(buf, sizeof(buf),
             "<div class='row'><div><label>Ceiling fan / switch</label>"
             "<input name='ha_fan_entity' maxlength='127' value=\"%s\" placeholder='fan.office_fan or switch.office_fan' autocapitalize='none' spellcheck='false'></div>"
             "<div><label>Alexa / media player</label>"
             "<input name='ha_echo_entity' maxlength='127' value=\"%s\" placeholder='media_player.office_echo' autocapitalize='none' spellcheck='false'></div></div>",
             ha_fan, ha_echo);
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "<div class='hint'>Leave a field blank to leave that control unmapped. Light controls require light.*, Echo requires media_player.*, "
        "fan accepts fan.* or switch.*.</div>"
        "</section>"));

    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req,
        "<section class='card' data-tab='system'><h2>Firmware updates</h2>"
        "<p class='muted'>Install a new firmware version from your browser. Your saved settings stay in place.</p>"
        "<a class='button-link secondary' href='/firmware'>Open firmware updater &rarr;</a>"
        "<p class='hint'>Set an administrator password before your first upload.</p></section>"
        "<section class='card full danger-card' data-tab='system'><h2>Reset configuration</h2>"
        "<p class='hint'>Restore firmware defaults for Wi-Fi, MQTT, display, sensors, and administrator access. The panel will restart.</p>"
        "<button class='danger' type='submit' form='reset-form'>Reset configuration</button></section>"
        "</div><div class='savebar'><div><strong id='change-status'>Settings ready</strong>"
        "<small>Saving applies all tabs and restarts the panel.</small></div><div class='actions'>"
        "<button class='secondary' id='discard-settings' type='reset'>Discard changes</button>"
        "<button id='save-settings' type='submit'>Save &amp; restart</button>"
        "</div></div></form>"));
    snprintf(buf, sizeof(buf), "<form id='reset-form' method='post' action='/reset'><input type='hidden' name='csrf' value='%s'></form>", web_security_token());
    ESP_ERROR_CHECK_WITHOUT_ABORT(send_chunk(req, buf));

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
    unsigned timeouts = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
        if (n <= 0) {
            free(body);
            return ESP_FAIL;
        }
        timeouts = 0;
        received += (size_t)n;
    }

    body[received] = '\0';
    *out_body = body;
    return ESP_OK;
}

static esp_err_t save_handler(httpd_req_t *req)
{
    if (!web_security_authorize(req, false)) return ESP_OK;
    char *body = NULL;
    esp_err_t err = read_request_body(req, &body);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form body");
        return ESP_OK;
    }

    char csrf[34];
    if (!form_get(body, "csrf", csrf, sizeof(csrf)) || !web_security_check_token(csrf)) {
        free(body);
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Session expired. Reload settings and try again.");
        return ESP_OK;
    }
    panel_settings_t updated = *settings_get();
    char tmp[256];

    char password[PANEL_ADMIN_PASSWORD_MAX + 2];
    if (form_get(body, "admin_password", password, sizeof(password)) && password[0]) {
        size_t length = strlen(password);
        bool valid = length >= 8 && length <= PANEL_ADMIN_PASSWORD_MAX;
        for (size_t i = 0; i < length; ++i) {
            if ((unsigned char)password[i] < 32 || (unsigned char)password[i] > 126) valid = false;
        }
        if (!valid) {
            free(body);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Administrator password must be 8-64 printable ASCII characters.");
            return ESP_OK;
        }
        strlcpy(updated.admin_password, password, sizeof(updated.admin_password));
    }

    /* One extra byte detects overlong input instead of silently truncating it.
     * Missing fields preserve the hostname for older clients. */
    char hostname[PANEL_HOSTNAME_MAX + 2];
    if (form_get(body, "hostname", hostname, sizeof(hostname))) {
        if (!settings_valid_hostname(hostname)) {
            free(body);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                "Invalid hostname. Use 1-32 lowercase letters, digits or hyphens; no leading/trailing hyphen or domain suffix.");
            return ESP_OK;
        }
        strlcpy(updated.hostname, hostname, sizeof(updated.hostname));
    }

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
    form_get(body, "mqtt_discovery_prefix", updated.mqtt_discovery_prefix,
             sizeof(updated.mqtt_discovery_prefix));
    form_get(body, "panel_id", updated.panel_id, sizeof(updated.panel_id));
    form_get(body, "display_title", updated.display_title, sizeof(updated.display_title));
    form_get(body, "ha_tapo_entity", updated.ha_tapo_entity, sizeof(updated.ha_tapo_entity));
    form_get(body, "ha_overhead_entity", updated.ha_overhead_entity, sizeof(updated.ha_overhead_entity));
    form_get(body, "ha_fan_entity", updated.ha_fan_entity, sizeof(updated.ha_fan_entity));
    form_get(body, "ha_echo_entity", updated.ha_echo_entity, sizeof(updated.ha_echo_entity));
    form_get(body, "timezone", updated.timezone, sizeof(updated.timezone));

    if (form_has(body, "clear_mqtt_password")) {
        updated.mqtt_password[0] = '\0';
    } else if (form_get(body, "mqtt_password", tmp, sizeof(tmp)) && tmp[0]) {
        strlcpy(updated.mqtt_password, tmp, sizeof(updated.mqtt_password));
    }

    struct sensor_field { const char *name; uint8_t *value; long min; long max; };
    struct sensor_field sensor_fields[] = {
        {"ambient_light_enabled", &updated.ambient_light_enabled, 0, 1},
        {"sensor_interval_seconds", &updated.sensor_interval_seconds, 1, 60},
        {"presence_enabled", &updated.presence_enabled, 0, 1},
        {"presence_address", &updated.presence_address, 42, 43},
        {"presence_max_m", &updated.presence_max_m, 3, 8},
        {"presence_trigger_sensitivity", &updated.presence_trigger_sensitivity, 0, 9},
        {"presence_keep_sensitivity", &updated.presence_keep_sensitivity, 0, 9},
        {"presence_hold_seconds", &updated.presence_hold_seconds, 2, 120},
    };
    for (size_t i = 0; i < sizeof(sensor_fields) / sizeof(sensor_fields[0]); ++i) {
        if (form_get(body, sensor_fields[i].name, tmp, sizeof(tmp))) {
            char *end = NULL;
            long value = strtol(tmp, &end, 10);
            if (!tmp[0] || *end || value < sensor_fields[i].min || value > sensor_fields[i].max) {
                free(body);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid sensor setting.");
                return ESP_OK;
            }
            *sensor_fields[i].value = (uint8_t)value;
        }
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

    for (int i = 0; i < PANEL_COLOR_PRESET_COUNT; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "color_%d", i);
        /* Missing fields preserve saved colors for older clients. */
        if (form_get(body, key, tmp, sizeof(tmp)) &&
            !parse_preset_color(tmp, &updated.color_presets[i])) {
            free(body);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "Invalid circle color. Use #RRGGBB.");
            return ESP_OK;
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

    if (!valid_display_title(updated.display_title)) {
        free(body);
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid display title. Use 1-31 printable characters.");
        return ESP_OK;
    }

    if (!valid_mqtt_prefix(updated.mqtt_discovery_prefix)) {
        free(body);
        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid Home Assistant MQTT discovery prefix.");
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

    size_t mqtt_base_len = strlen(updated.mqtt_base_topic);
    while (mqtt_base_len > 0 && updated.mqtt_base_topic[mqtt_base_len - 1] == '/') {
        updated.mqtt_base_topic[--mqtt_base_len] = '\0';
    }
    if (updated.mqtt_base_topic[0] == '\0') {
        strlcpy(updated.mqtt_base_topic, "home/7c-panel", sizeof(updated.mqtt_base_topic));
    }
    size_t discovery_len = strlen(updated.mqtt_discovery_prefix);
    while (discovery_len > 0 && updated.mqtt_discovery_prefix[discovery_len - 1] == '/') {
        updated.mqtt_discovery_prefix[--discovery_len] = '\0';
    }
    if (updated.mqtt_discovery_prefix[0] == '\0') {
        strlcpy(updated.mqtt_discovery_prefix, "homeassistant",
                sizeof(updated.mqtt_discovery_prefix));
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
    if (!web_security_authorize(req, false)) return ESP_OK;
    char *body = NULL, csrf[34];
    esp_err_t err = read_request_body(req, &body);
    bool valid = err == ESP_OK && form_get(body, "csrf", csrf, sizeof(csrf)) && web_security_check_token(csrf);
    free(body);
    if (!valid) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "Session expired. Reload settings and try again.");
        return ESP_OK;
    }
    err = settings_reset();
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

    web_security_init();
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
    ESP_ERROR_CHECK(ota_update_register(s_server));

    if (network_is_connected() && network_get_sta_ip()) {
        ESP_LOGI(TAG, "Configuration UI: http://%s/", network_get_sta_ip());
    }
    if (network_config_ap_active()) {
        ESP_LOGI(TAG, "Configuration UI: http://192.168.4.1/");
    }

    return ESP_OK;
}


