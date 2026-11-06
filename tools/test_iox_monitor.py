#!/usr/bin/env python3
"""Exercise the production IO expander reset detector with a fake clock.

A 3.3 V dip (for example hot-plugging a sensor) can reset the 7C-BOX IO
expander; its pins become inputs and the backlight enable's pull-down turns the
screen off. The monitor must restore it without ever acting on pins whose
read-back was not proven, or on bus errors.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TEST = r'''
#include "iox_monitor.c"
#include <assert.h>
#define S 1000000LL
#define ALL 0xFFFF
#define DISP (1u << 2)
#define VDD_EN (1u << 5)
static iox_monitor_t m;
static int64_t now;
static bool check(uint16_t driven, bool ok, uint16_t levels) {
 assert(iox_monitor_due(&m, now));
 bool repair = iox_monitor_update(&m, now, driven, ok, levels);
 assert(!iox_monitor_due(&m, now) && m.next_check_us == now + S);
 return repair;
}
int main(void) {
 /* Bus errors before calibration neither calibrate nor repair. */
 assert(iox_monitor_due(&m, 0));
 assert(!check(ALL, false, 0) && !m.calibrated);
 now += S;

 /* Calibrate: only pins that read back high while driven high are trusted.
  * Pin 9 reads low although driven high (input register does not mirror it). */
 assert(!check(ALL, true, (uint16_t)(ALL & ~(1u << 9))));
 assert(m.calibrated && m.verified == (uint16_t)(ALL & ~(1u << 9)));

 /* An unverified pin reading low is never treated as a fault. */
 for (int i = 0; i < 5; i++) { now += S; assert(!check(ALL, true, (uint16_t)(ALL & ~(1u << 9)))); }

 /* Expander reset: backlight enable falls (pull-down). One reading is not
  * enough; the second consecutive one triggers a repair. */
 uint16_t reset_levels = (uint16_t)(ALL & ~DISP & ~(1u << 9));
 now += S; assert(!check(ALL, true, reset_levels) && m.mismatches == 1);
 now += S; assert(check(ALL, true, reset_levels) && m.mismatches == 0);

 /* Bus errors do not count as evidence and do not clear a pending mismatch. */
 now += S; assert(!check(ALL, true, reset_levels) && m.mismatches == 1);
 now += S; assert(!check(ALL, false, 0) && m.mismatches == 1);

 /* Repairs are rate limited to one per 10 s while the fault persists. */
 now += S; assert(!check(ALL, true, reset_levels));
 int64_t first = now - 3 * S; /* time of the first repair */
 while (now < first + IOX_MONITOR_REPAIR_US - S) { now += S; assert(!check(ALL, true, reset_levels)); }
 now += S; assert(check(ALL, true, reset_levels) && m.failed_repairs == 2);

 /* Once the restore works, a passing check clears the failure history. */
 now += S; assert(!check(ALL, true, (uint16_t)(ALL & ~(1u << 9))));
 assert(m.failed_repairs == 0 && m.mismatches == 0);

 /* Pins driven low (screen asleep) are ignored even if they read low. */
 now += S; assert(!check((uint16_t)(ALL & ~DISP), true, reset_levels));
 now += S; assert(!check((uint16_t)(ALL & ~DISP), true, reset_levels) && m.mismatches == 0);

 /* After three repairs that never produce a passing check, back off to 60 s. */
 uint16_t dead = (uint16_t)(ALL & ~VDD_EN & ~(1u << 9));
 int repairs = 0;
 int64_t last = 0;
 for (int i = 0; i < 200 && repairs < 4; i++) {
  now += S;
  if (check(ALL, true, dead)) {
   if (repairs == 3) assert(now - last >= IOX_MONITOR_BACKOFF_US);
   else if (repairs > 0) assert(now - last >= IOX_MONITOR_REPAIR_US && now - last < IOX_MONITOR_BACKOFF_US);
   last = now; repairs++;
  }
 }
 assert(repairs == 4 && m.failed_repairs == 4);

 /* A pin first driven high later (screen started asleep) is learned. */
 iox_monitor_t fresh = {0};
 m = fresh; now += S;
 assert(!check((uint16_t)(ALL & ~DISP), true, (uint16_t)(ALL & ~DISP)) && !(m.verified & DISP));
 now += S; assert(!check(ALL, true, ALL) && (m.verified & DISP));
 now += S; assert(!check(ALL, true, (uint16_t)(ALL & ~DISP)));
 now += S; assert(check(ALL, true, (uint16_t)(ALL & ~DISP)));

 /* An expander whose input register reads all zero is never "repaired". */
 m = fresh;
 for (int i = 0; i < 30; i++) { now += S; assert(!check(ALL, true, 0)); }
 assert(m.calibrated && m.verified == 0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d/'test.c').write_text(TEST)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'main'),
                    str(d/'test.c'), '-o', str(d/'test')], check=True)
    subprocess.run([str(d/'test')], check=True)
print('IO expander calibration, debounce, rate-limit, backoff, learning and bus-error checks passed')
