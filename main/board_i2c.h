#pragma once
#include "driver/i2c_master.h"

/* Borrowed shared bus; callers must not delete it. NULL in the emulator. */
i2c_master_bus_handle_t board_7c_i2c_bus(void);
