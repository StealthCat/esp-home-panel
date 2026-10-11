#include "ota_update.h"
#include "web_security.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";
static bool s_pending_verify;
static int64_t s_boot_started;
static esp_timer_handle_t s_boot_timer;
extern const unsigned char firmware_html_start[] asm("_binary_firmware_html_start");
extern const unsigned char firmware_html_end[] asm("_binary_firmware_html_end");

static void boot_timeout(void *arg)
{
    (void)arg;
    ESP_LOGE(TAG, "New firmware did not complete startup checks; rebooting for rollback");
    esp_restart();
}

void ota_update_boot_begin(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    s_pending_verify = running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
                       state == ESP_OTA_IMG_PENDING_VERIFY;
    if (!s_pending_verify) return;
    s_boot_started = esp_timer_get_time();
    const esp_timer_create_args_t timer = {.callback = boot_timeout, .name = "ota_boot_check"};
    ESP_ERROR_CHECK(esp_timer_create(&timer, &s_boot_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(s_boot_timer, 90LL * 1000000));
    ESP_LOGI(TAG, "Testing new firmware before confirming OTA boot");
}

void ota_update_boot_poll(bool local_services_ready)
{
    /* Board/UI setup has succeeded and the main loop must remain alive.
     * An unavailable AP, Internet or MQTT broker must not invalidate firmware. */
    if (!s_pending_verify || !local_services_ready ||
        esp_timer_get_time() - s_boot_started < 30LL * 1000000) return;
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not confirm firmware: %s", esp_err_to_name(err));
        return; /* Boot deadline still enforces recovery. */
    }
    s_pending_verify = false;
    esp_timer_stop(s_boot_timer);
    esp_timer_delete(s_boot_timer);
    s_boot_timer = NULL;
    ESP_LOGI(TAG, "Firmware startup checks passed; update confirmed");
}

static esp_err_t fail_request(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, message);
    /* Unconsumed upload bytes must not become a second request. */
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
    return ESP_OK;
}

static esp_err_t firmware_page(httpd_req_t *req)
{
    if (!web_security_authorize(req, false)) return ESP_OK;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)firmware_html_start,
                          firmware_html_end - firmware_html_start - 1);
}

static esp_err_t firmware_status(httpd_req_t *req)
{
    if (!web_security_authorize(req, true)) return ESP_OK;
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t state;
    bool pending = running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
                   state == ESP_OTA_IMG_PENDING_VERIFY;
    char sha[65];
    esp_app_get_elf_sha256(sha, sizeof(sha));
    cJSON *json = cJSON_CreateObject();
    if (!json) return fail_request(req, "503 Service Unavailable", "Not enough memory.");
    cJSON_AddStringToObject(json, "version", app->version);
    cJSON_AddStringToObject(json, "build", sha);
    cJSON_AddStringToObject(json, "slot", running ? running->label : "unknown");
    cJSON_AddNumberToObject(json, "max_size", next ? next->size : 0);
    cJSON_AddBoolToObject(json, "pending", pending);
    cJSON_AddStringToObject(json, "token", web_security_token());
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return fail_request(req, "503 Service Unavailable", "Not enough memory.");
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, body);
    free(body);
    return err;
}

static bool valid_image_header(const unsigned char *data)
{
    esp_image_header_t image;
    esp_app_desc_t app;
    memcpy(&image, data, sizeof(image));
    memcpy(&app, data + sizeof(image) + sizeof(esp_image_segment_header_t), sizeof(app));
    return image.magic == ESP_IMAGE_HEADER_MAGIC && image.chip_id == ESP_CHIP_ID_ESP32S3 &&
           image.segment_count > 0 && image.segment_count <= 16 &&
           app.magic_word == ESP_APP_DESC_MAGIC_WORD &&
           memchr(app.project_name, '\0', sizeof(app.project_name)) &&
           memchr(app.version, '\0', sizeof(app.version)) &&
           strcmp(app.project_name, esp_app_get_description()->project_name) == 0;
}

static esp_err_t firmware_upload(httpd_req_t *req)
{
    /* httpd executes handlers serially: no concurrent update or settings write.
     * Only the inactive app slot is written. NVS/bootloader/table are untouched. */
    if (!web_security_authorize(req, true)) {
        httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    char token[34], content_type[40];
    if (httpd_req_get_hdr_value_str(req, "X-OTA-Token", token, sizeof(token)) != ESP_OK ||
        !web_security_check_token(token)) {
        return fail_request(req, "403 Forbidden", "Upload session expired. Reload the firmware page.");
    }
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/octet-stream") != 0) {
        return fail_request(req, "415 Unsupported Media Type", "Upload the application firmware.bin as a binary file.");
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t state;
    if (!running || !next || next->address == running->address) {
        return fail_request(req, "409 Conflict", "No inactive OTA slot. Install the complete firmware over USB first.");
    }
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        return fail_request(req, "409 Conflict", "Startup checks are still running. Try again in 30 seconds.");
    }
    const size_t header_size = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    if (req->content_len < header_size || req->content_len > next->size) {
        return fail_request(req, "400 Bad Request", "Invalid file size. Use the application firmware.bin, not a merged flash image.");
    }
    unsigned char *buffer = malloc(4096);
    if (!buffer) return fail_request(req, "503 Service Unavailable", "Not enough memory to start an update.");
    esp_ota_handle_t handle = 0;
    bool begun = false;
    size_t received = 0, buffered = 0;
    unsigned timeouts = 0;
    int64_t deadline = esp_timer_get_time() + 10LL * 60 * 1000000;
    const char *error = NULL;
    const char *status = "400 Bad Request";
    char expected_build[65];
    while (received < req->content_len) {
        if (esp_timer_get_time() > deadline) { error = "Upload timed out. Current firmware is unchanged."; break; }
        size_t wanted = begun ? 4096 : header_size - buffered;
        if (wanted > req->content_len - received) wanted = req->content_len - received;
        int count = httpd_req_recv(req, (char *)buffer + buffered, wanted);
        if (count == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts < 3) continue;
            error = "Upload stalled. Current firmware is unchanged."; break;
        }
        if (count <= 0) { error = "Upload interrupted. Current firmware is unchanged."; break; }
        timeouts = 0;
        received += count;
        buffered += count;
        if (!begun) {
            if (buffered < header_size) continue;
            if (!valid_image_header(buffer)) { error = "This is not an ESP Home Panel ESP32-S3 application image."; break; }
            esp_app_desc_t candidate;
            memcpy(&candidate, buffer + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(candidate));
            for (size_t i = 0; i < sizeof(candidate.app_elf_sha256); ++i) {
                snprintf(expected_build + 2 * i, 3, "%02x", candidate.app_elf_sha256[i]);
            }
            esp_err_t err = esp_ota_begin(next, req->content_len, &handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
                error = "Could not prepare the inactive firmware slot."; status = "500 Internal Server Error"; break;
            }
            begun = true;
        }
        if (esp_ota_write(handle, buffer, buffered) != ESP_OK) {
            error = "Firmware write failed. Current firmware is unchanged."; status = "500 Internal Server Error"; break;
        }
        buffered = 0;
    }
    free(buffer);
    if (error) {
        if (begun) esp_ota_abort(handle);
        return fail_request(req, status, error);
    }
    /* end() frees the handle even when verification fails. */
    if (esp_ota_end(handle) != ESP_OK) {
        return fail_request(req, "400 Bad Request", "Firmware integrity verification failed. Current firmware is unchanged.");
    }
    if (esp_ota_set_boot_partition(next) != ESP_OK) {
        return fail_request(req, "500 Internal Server Error", "Could not select the verified firmware for boot.");
    }
    char reply[160];
    snprintf(reply, sizeof(reply), "{\"rebooting\":true,\"build\":\"%s\"}", expected_build);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_sendstr(req, reply);
    ESP_LOGI(TAG, "Verified firmware written to %s; rebooting", next->label);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

esp_err_t ota_update_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/firmware", .method = HTTP_GET, .handler = firmware_page},
        {.uri = "/firmware/status", .method = HTTP_GET, .handler = firmware_status},
        {.uri = "/firmware/upload", .method = HTTP_POST, .handler = firmware_upload},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
