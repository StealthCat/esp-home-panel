#include "display_policy.h"
#include <math.h>

void display_policy_touch(display_policy_t *s, const panel_settings_t *cfg, int64_t now)
{
    s->touch_until_us = now + (int64_t)cfg->touch_wake_minutes * 60000000;
    s->asleep = false;
}

/* Called every 250 ms. Timers use monotonic time, independent of NTP/MQTT. */
uint8_t display_policy_step(display_policy_t *s, const panel_settings_t *cfg,
    int64_t now, bool presence_valid, bool occupied, bool lux_valid, float lux, bool pwm)
{
    bool vacant = cfg->sleep_enabled && cfg->presence_enabled && presence_valid && !occupied;
    if (!vacant) s->vacant_tracking = false;
    else if (!s->vacant_tracking) {
        s->vacant_since_us = now;
        s->vacant_tracking = true;
    }
    s->asleep = vacant && now - s->vacant_since_us >= (int64_t)cfg->sleep_delay_seconds * 1000000
        && now >= s->touch_until_us;
    unsigned target = cfg->backlight_percent;
    if (cfg->auto_brightness && cfg->ambient_light_enabled && pwm && lux_valid && isfinite(lux) && lux >= 0) {
        if (!s->have_lux) s->filtered_lux = lux;
        else s->filtered_lux += 0.12f * (lux - s->filtered_lux);
        s->have_lux = true;
        /* Perceptual curve: dark -> configured floor, 1000 lx -> ceiling. */
        float fraction = fminf(1.0f, log1pf(s->filtered_lux) / log1pf(1000.0f));
        target = cfg->brightness_min + (unsigned)lroundf(fraction * (cfg->brightness_max - cfg->brightness_min));
    } else s->have_lux = false;
    /* A tap/presence must be able to recover a manually dark screen. */
    if (target == 0 && (now < s->touch_until_us || (cfg->sleep_enabled && presence_valid && occupied))) target = 10;
    if (s->asleep) return 0;
    if (!pwm) return target ? 100 : 0;
    int delta = (int)target - s->brightness;
    if (delta > 3) delta = 3;
    if (delta < -3) delta = -3;
    s->brightness = (uint8_t)((int)s->brightness + delta);
    return s->brightness;
}
