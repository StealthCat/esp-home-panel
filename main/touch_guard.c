#include "touch_guard.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "touch_guard";

#define TOUCH_GUARD_LOG_INTERVAL_US 5000000LL

/* Only the LVGL task reads touch, so this state needs no locking. */
static esp_err_t (*s_read_data)(esp_lcd_touch_handle_t tp);
static uint32_t s_unreported_failures;
static int64_t s_next_log_us;

static esp_err_t guarded_read_data(esp_lcd_touch_handle_t tp)
{
    esp_err_t err = s_read_data(tp);
    if (err == ESP_OK) return ESP_OK;

    /* Report a release rather than replaying the previous press. The GT911
     * driver does the same when the coordinate read of a frame fails. */
    portENTER_CRITICAL(&tp->data.lock);
    tp->data.points = 0;
    portEXIT_CRITICAL(&tp->data.lock);

    s_unreported_failures++;
    int64_t now = esp_timer_get_time();
    if (now >= s_next_log_us) {
        ESP_LOGW(TAG, "Ignored %lu touch read failure(s) (last: %s); "
                 "check accessories and wiring on the shared I2C bus",
                 (unsigned long)s_unreported_failures, esp_err_to_name(err));
        s_unreported_failures = 0;
        s_next_log_us = now + TOUCH_GUARD_LOG_INTERVAL_US;
    }
    return ESP_OK;
}

void touch_guard_install(esp_lcd_touch_handle_t touch)
{
    if (!touch || !touch->read_data || touch->read_data == guarded_read_data) return;
    s_read_data = touch->read_data;
    touch->read_data = guarded_read_data;
}
