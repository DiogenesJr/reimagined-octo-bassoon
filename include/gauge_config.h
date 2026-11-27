#pragma once

#include <cstdint>
#include <lvgl.h>
#include "GaugeLogic.h"

// GAUGE_TYPE is set by the build environment in platformio.ini
#define GAUGE_COOLANT_TEMP  1
#define GAUGE_OIL_TEMP      2
#define GAUGE_OIL_PRESSURE  3

struct GaugeConfig {
  const char *caption;
  int min_value;        // whole units
  int max_value;        // whole units
  int tick_count;       // 240 must divide evenly by (tick_count - 1)
  uint8_t icon_red;
  uint8_t icon_green;
  uint8_t icon_blue;
  uint32_t can_id;      // 0 = CAN signal not yet defined
  int (*decode)(const uint8_t *data);
  const lv_image_dsc_t *icon;  // tinted with the icon color; nullptr = droplet symbol
};

#ifndef GAUGE_TYPE
  #error "GAUGE_TYPE is not defined; build one of the gauge environments in platformio.ini"
#elif GAUGE_TYPE == GAUGE_COOLANT_TEMP
// degrees C
#include "../src/images/icon_coolant.h"
constexpr GaugeConfig GAUGE = {"COOLANT", -20, 140, 9, 30, 130, 210, 0x345, process_coolant_temp_value,
                               &icon_coolant};
#elif GAUGE_TYPE == GAUGE_OIL_TEMP
// degrees C
#include "../src/images/icon_oil_temp.h"
constexpr GaugeConfig GAUGE = {"OIL TEMP", 0, 160, 9, 191, 128, 0, 0x345, process_oil_temp_value,
                               &icon_oil_temp};
#elif GAUGE_TYPE == GAUGE_OIL_PRESSURE
// PSI
// PLACEHOLDER CAN ID and format for bench testing, not from a real vehicle
constexpr GaugeConfig GAUGE = {"OIL PSI", 0, 100, 6, 220, 180, 40, 0x601, process_oil_pressure_value,
                               nullptr};
#else
  #error "Unknown GAUGE_TYPE"
#endif

static_assert(240 % (GAUGE.tick_count - 1) == 0, "ticks must be evenly spaced over 240 degrees");
