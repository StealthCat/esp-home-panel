#pragma once
#include <stdbool.h>
#include <stdint.h>

/*
 * Detects an IO expander that has lost its configuration, for example after a
 * 3.3 V supply dip from hot-plugging an accessory resets it. On the 7C-BOX the
 * backlight enable has a pull-down, so a reset expander leaves the screen dark
 * while the ESP32 keeps running.
 *
 * The expander's mode/output registers are write-only, so detection compares
 * its input register with the pins the firmware drives high. A pin is trusted
 * only after it has read back high while driven high; an expander whose input
 * register does not mirror its outputs is therefore never "repaired".
 */
typedef struct {
    uint16_t verified;          /* pins seen reading back high while driven high */
    bool calibrated;
    uint8_t mismatches;         /* consecutive checks with a verified pin low */
    uint8_t failed_repairs;     /* repairs not yet followed by a passing check */
    int64_t next_check_us;
    int64_t next_repair_us;
} iox_monitor_t;

#define IOX_MONITOR_CHECK_US          1000000LL
#define IOX_MONITOR_CONFIRMATIONS     2
#define IOX_MONITOR_REPAIR_US         10000000LL
#define IOX_MONITOR_BACKOFF_REPAIRS   3
#define IOX_MONITOR_BACKOFF_US        60000000LL

bool iox_monitor_due(const iox_monitor_t *monitor, int64_t now_us);

/* Feed one input-register read. driven_high holds the pins currently driven
 * high. Returns true when the expander should be re-initialized now. */
bool iox_monitor_update(iox_monitor_t *monitor, int64_t now_us, uint16_t driven_high,
                        bool read_ok, uint16_t levels);
