#include "iox_monitor.h"

bool iox_monitor_due(const iox_monitor_t *monitor, int64_t now_us)
{
    return now_us >= monitor->next_check_us;
}

bool iox_monitor_update(iox_monitor_t *monitor, int64_t now_us, uint16_t driven_high,
                        bool read_ok, uint16_t levels)
{
    monitor->next_check_us = now_us + IOX_MONITOR_CHECK_US;
    if (!read_ok) return false; /* bus error: no evidence either way */

    uint16_t missing = driven_high & monitor->verified & (uint16_t)~levels;
    if (!missing) {
        /* Healthy. Learn pins that mirror their output, e.g. the backlight
         * enable once the screen wakes after starting asleep. */
        monitor->verified |= driven_high & levels;
        monitor->calibrated = true;
        monitor->mismatches = 0;
        monitor->failed_repairs = 0;
        return false;
    }

    if (monitor->mismatches < IOX_MONITOR_CONFIRMATIONS) monitor->mismatches++;
    if (monitor->mismatches < IOX_MONITOR_CONFIRMATIONS || now_us < monitor->next_repair_us) {
        return false;
    }

    monitor->mismatches = 0;
    if (monitor->failed_repairs < UINT8_MAX) monitor->failed_repairs++;
    monitor->next_repair_us = now_us + (monitor->failed_repairs >= IOX_MONITOR_BACKOFF_REPAIRS
        ? IOX_MONITOR_BACKOFF_US : IOX_MONITOR_REPAIR_US);
    return true;
}
