#pragma once

#include <cstdint>

// Moving-average smoothing over the last AVERAGE_VALUES readings, returned
// scaled x10 to preserve one decimal place of precision as an int.
int get_moving_average(int new_value);

// Reset the moving-average history (test hook; not used by firmware).
void reset_moving_average(void);

// Decode a temperature byte with a -40 offset, in deg C. Used for coolant temp
// (CAN message 0x551, Nissan-specific) and the placeholder oil temp message.
int process_scale_value(const uint8_t *byte_data);

// Decode the placeholder oil pressure message: byte 0 in PSI.
int process_oil_pressure_value(const uint8_t *byte_data);
