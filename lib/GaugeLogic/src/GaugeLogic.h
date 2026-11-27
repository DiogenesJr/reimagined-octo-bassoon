#pragma once

#include <cstdint>

// Moving-average smoothing over the last AVERAGE_VALUES readings, returned
// scaled x10 to preserve one decimal place of precision as an int.
int get_moving_average(int new_value);

// Reset the moving-average history (test hook; not used by firmware).
void reset_moving_average(void);

// Decode the temperatures in CAN message 0x345 (2022+ Toyota GR86 / Subaru BRZ),
// in deg C. Source: github.com/timurrrr/ft86, can_bus/gen2.md
int process_oil_temp_value(const uint8_t *byte_data);      // byte 3, offset -40
int process_coolant_temp_value(const uint8_t *byte_data);  // byte 4, offset -40

// Decode the placeholder oil pressure message: byte 0 in PSI.
int process_oil_pressure_value(const uint8_t *byte_data);
