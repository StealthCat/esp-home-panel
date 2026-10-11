/* Included by ui.c: all LVGL work stays on the LVGL task. */
static lv_obj_t *s_controls_page, *s_weather_page, *s_weather_status, *s_forecast_status;
static lv_obj_t *s_weather_heading, *s_weather_summary, *s_weather_details;
static lv_obj_t *s_weather_fields[WEATHER_MAX_FIELDS];
static lv_obj_t *s_forecast_cards[WEATHER_DAYS], *s_page_indicator, *s_header_hit;
static weather_snapshot_t *s_weather_view;
static uint32_t s_weather_revision;
static bool s_show_weather;
static display_policy_t s_display_policy;
static lv_obj_t *s_wake_shield;
static int s_last_backlight = -1;

static void set_page(bool weather)
{
    s_show_weather = weather && settings_get()->weather_enabled;
    if (s_show_weather) {
        lv_obj_add_flag(s_controls_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_weather_page, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_controls_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_weather_page, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(s_page_indicator, s_show_weather ? "<  Weather  2/2  >" : "<  Controls  1/2  >");
}

static void header_navigation_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE) {
        lv_indev_t *input = lv_indev_active();
        lv_dir_t dir = input ? lv_indev_get_gesture_dir(input) : LV_DIR_NONE;
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
            set_page(!s_show_weather);
            lv_indev_wait_release(input);
        }
    } else if (code == LV_EVENT_SHORT_CLICKED) set_page(!s_show_weather);
}

static void wake_cb(lv_event_t *e)
{
    display_policy_touch(&s_display_policy, settings_get(), esp_timer_get_time());
    /* The shield owns the complete press/release sequence. Remove it only
     * after release so this touch cannot activate a control underneath. */
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) lv_obj_add_flag(s_wake_shield, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *weather_text(lv_obj_t *parent, const char *text, int x, int y, int width,
                              const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = make_label(parent, text, x, y, font, color);
    lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    return label;
}

static void weather_icon(lv_obj_t *parent, const char *condition)
{
    bool sun = !strcmp(condition, "sunny") || !strcmp(condition, "clear-night");
    bool rain = strstr(condition, "rain") || strstr(condition, "snow") || strstr(condition, "lightning");
    if (!strcmp(condition, "partlycloudy")) {
        lv_obj_t *sun_dot = lv_obj_create(parent);
        lv_obj_remove_style_all(sun_dot);
        lv_obj_remove_flag(sun_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(sun_dot, 72, 26);
        lv_obj_set_size(sun_dot, 22, 22);
        lv_obj_set_style_radius(sun_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(sun_dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(sun_dot, c(T->warm), 0);
    }
    /* Native geometry inherits theme colors and needs no bitmap RAM. */
    for (int i = 0; i < (sun ? 1 : 3); ++i) {
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_remove_style_all(dot);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(dot, 58 + i * 10, 34 - (i == 1 ? 5 : 0));
        lv_obj_set_size(dot, sun ? 28 : 22, sun ? 28 : 22);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, c(sun ? T->warm : T->muted), 0);
    }
    if (!strcmp(condition, "clear-night")) {
        lv_obj_t *cutout = lv_obj_create(parent);
        lv_obj_remove_style_all(cutout);
        lv_obj_remove_flag(cutout, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(cutout, 69, 28);
        lv_obj_set_size(cutout, 23, 23);
        lv_obj_set_style_radius(cutout, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(cutout, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(cutout, c(T->card_bg), 0);
    }
    if (rain) for (int i = 0; i < 3; ++i) {
        lv_obj_t *drop = lv_obj_create(parent);
        lv_obj_remove_style_all(drop);
        lv_obj_remove_flag(drop, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(drop, 62 + i * 12, 59);
        lv_obj_set_size(drop, 3, 7);
        lv_obj_set_style_bg_opa(drop, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(drop, c(T->accent), 0);
    }
}

static void update_weather_view(void)
{
    if (!s_weather_view || !weather_copy(s_weather_view)) return;
    weather_snapshot_t *w = s_weather_view;
    int64_t now = esp_timer_get_time();
    unsigned station_age = w->station_age_seconds + (unsigned)((now - w->station_received_us) / 1000000);
    unsigned delivery_age = (unsigned)((now - w->station_received_us) / 1000000);
    unsigned forecast_age = (unsigned)((now - w->forecast_received_us) / 1000000);
    char status[128];
    if (!w->station_received_us) snprintf(status, sizeof(status), "Waiting for station - check HA weather package");
    else if (!w->station_available) snprintf(status, sizeof(status), "Station unavailable");
    else snprintf(status, sizeof(status), "%sUpdated %u min ago%s", station_age > 900 || delivery_age > 120 ? "Stale - " : "", station_age / 60, w->truncated ? " | showing 48 sensors" : "");
    lv_label_set_text(s_weather_status, status);
    if (!w->forecast_received_us) snprintf(status, sizeof(status), "DAILY FORECAST  |  Waiting for Home Assistant");
    else if (!w->forecast_available) snprintf(status, sizeof(status), "DAILY FORECAST  |  Unavailable - select a daily forecast entity");
    else snprintf(status, sizeof(status), "DAILY FORECAST  |  %sUpdated %u min ago", forecast_age > 1800 ? "Stale - " : "", forecast_age / 60);
    lv_label_set_text(s_forecast_status, status);
    if (w->revision == s_weather_revision) return;
    s_weather_revision = w->revision;
    lv_label_set_text(s_weather_heading, w->name[0] ? w->name : "Weather station");
    char summary[128];
    if (w->station_available && w->field_count) snprintf(summary, sizeof(summary), "%s %s", w->fields[0].value, w->fields[0].unit);
    else snprintf(summary, sizeof(summary), "--");
    lv_label_set_text(s_weather_summary, summary);
    for (unsigned i = 0; i < WEATHER_MAX_FIELDS; ++i) {
        lv_obj_t *label = s_weather_fields[i];
        if (i >= w->field_count) { lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
        char text[80];
        snprintf(text, sizeof(text), "%s %s", w->fields[i].value, w->fields[i].unit);
        lv_label_set_text(lv_obj_get_child(label, 0), w->fields[i].label);
        lv_label_set_text(lv_obj_get_child(label, 1), text);
    }
    for (unsigned i = 0; i < WEATHER_DAYS; ++i) {
        lv_obj_t *card = s_forecast_cards[i];
        lv_obj_clean(card);
        if (T->magic) add_magic_card_ornaments(card, 144, 152);
        if (!w->forecast_available || i >= w->day_count) {
            weather_text(card, "--", 12, 60, 120, &lv_font_montserrat_22, T->muted);
            continue;
        }
        weather_day_t *day = &w->days[i];
        weather_text(card, day->date, 12, 9, 124, &lv_font_montserrat_14, T->text);
        weather_icon(card, day->condition);
        char condition[40];
        snprintf(condition, sizeof(condition), "%s", day->condition);
        for (char *p = condition; *p; ++p) if (*p == '-') *p = ' ';
        if (!strcmp(condition, "partlycloudy")) snprintf(condition, sizeof(condition), "Partly cloudy");
        weather_text(card, condition, 10, 70, 128, &lv_font_montserrat_14, T->muted);
        weather_text(card, day->high, 14, 88, 128, &lv_font_montserrat_22, T->text);
        char low[48]; snprintf(low, sizeof(low), "Low %s", day->low);
        weather_text(card, low, 14, 115, 128, &lv_font_montserrat_14, T->muted);
        weather_text(card, day->rain, 24, 134, 110, &lv_font_montserrat_14, T->accent);
    }
}

static void display_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const panel_settings_t *cfg = settings_get();
    int64_t now = esp_timer_get_time();
    if (!s_display_policy.asleep && lv_display_get_inactive_time(s_disp) < 300)
        display_policy_touch(&s_display_policy, cfg, now);
    presence_reading_t presence = c4001_reading();
    ambient_light_reading_t light = sensors_ambient_light();
    bool presence_valid = presence.available && now - presence.sampled_us < 3000000;
    bool light_valid = light.available && now - light.sampled_us < ((int64_t)cfg->sensor_interval_seconds * 3 + 5) * 1000000;
    uint8_t brightness = display_policy_step(&s_display_policy, cfg, now, presence_valid, presence.occupied,
        light_valid, light.lux, board_7c_has_pwm_backlight());
    if (brightness == 0) lv_obj_remove_flag(s_wake_shield, LV_OBJ_FLAG_HIDDEN);
    else if (s_last_backlight == 0) {
        lv_indev_t *input = lv_indev_get_next(NULL);
        if (!input || lv_indev_get_state(input) == LV_INDEV_STATE_RELEASED)
            lv_obj_add_flag(s_wake_shield, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_last_backlight != brightness && board_7c_set_backlight(brightness) == ESP_OK)
        s_last_backlight = brightness;
    if (cfg->weather_enabled) update_weather_view();
}

static void create_weather_page(lv_obj_t *root)
{
    if (!settings_get()->weather_enabled) return;
    s_weather_page = lv_obj_create(root);
    lv_obj_remove_style_all(s_weather_page);
    lv_obj_set_size(s_weather_page, 800, 480);
    lv_obj_remove_flag(s_weather_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *current = make_card(s_weather_page, 14, 78, 246, 214);
    s_weather_heading = weather_text(current, "Weather station", 16, 14, 214, &lv_font_montserrat_22, T->text);
    lv_label_set_long_mode(s_weather_heading, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(s_weather_heading, 54);
    s_weather_summary = weather_text(current, "--", 16, 76, 214, &font_outfit_semibold_48, T->text);
    s_weather_status = weather_text(current, "Waiting for Home Assistant", 16, 151, 214, &lv_font_montserrat_14, T->muted);
    lv_label_set_long_mode(s_weather_status, LV_LABEL_LONG_MODE_WRAP);
    s_weather_details = make_card(s_weather_page, 270, 78, 516, 214);
    lv_obj_add_flag(s_weather_details, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_weather_details, LV_DIR_VER);
    /* Three visible rows; long names/values scroll within their own cell. */
    for (unsigned i = 0; i < WEATHER_MAX_FIELDS; ++i) {
        lv_obj_t *cell = lv_obj_create(s_weather_details);
        lv_obj_remove_style_all(cell);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(cell, 14 + (i % 2) * 247, 12 + (i / 2) * 66);
        lv_obj_set_size(cell, 230, 58);
        lv_obj_t *name = weather_text(cell, "", 0, 0, 230, &lv_font_montserrat_14, T->muted);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_t *value = weather_text(cell, "", 0, 22, 230, &lv_font_montserrat_22, T->text);
        lv_label_set_long_mode(value, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        s_weather_fields[i] = cell;
        lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
    }
    s_forecast_status = weather_text(s_weather_page, "DAILY FORECAST  |  Waiting for Home Assistant", 18, 299, 764, &lv_font_montserrat_14, T->muted);
    for (int i = 0; i < WEATHER_DAYS; ++i)
        s_forecast_cards[i] = make_card(s_weather_page, 14 + i * 156, 320, 148, 156);
    s_page_indicator = weather_text(root, "<  Controls  1/2  >", 82, 62, 260, &lv_font_montserrat_14, T->muted);
    s_header_hit = lv_obj_create(root);
    lv_obj_remove_style_all(s_header_hit);
    lv_obj_set_size(s_header_hit, 800, 77);
    lv_obj_remove_flag(s_header_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_header_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_header_hit, header_navigation_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_header_hit, header_navigation_cb, LV_EVENT_SHORT_CLICKED, NULL);
    set_page(false);
}

static void create_display_behavior(void)
{
    s_wake_shield = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_wake_shield);
    lv_obj_set_size(s_wake_shield, 800, 480);
    lv_obj_remove_flag(s_wake_shield, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wake_shield, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_wake_shield, wake_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_wake_shield, wake_cb, LV_EVENT_RELEASED, NULL);
    s_display_policy.brightness = settings_get()->backlight_percent;
    lv_timer_create(display_timer_cb, 250, NULL);
}
