#pragma once

#include <cstdint>

// Moving-average smoothing over the last AVERAGE_VALUES readings, returned
// scaled x10 to preserve one decimal place of precision as an int.
int get_moving_average(int new_value);

// Reset the moving-average history (test hook; not used by firmware).
void reset_moving_average(void);

// Decode the coolant temp byte from CAN message 0x551 (Nissan-specific -40 offset).
int process_scale_value(const uint8_t *byte_data);
