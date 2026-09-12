#include "GaugeLogic.h"
#include <cmath>

static const int AVERAGE_VALUES = 10;

static int values[AVERAGE_VALUES] = {0};
static int index_ = 0;
static int count = 0;
static double sum = 0;

int get_moving_average(int new_value) {
    // Subtract the value being replaced
    sum -= values[index_];

    // Insert the new value
    values[index_] = new_value;
    sum += new_value;

    // Update index and count
    index_ = (index_ + 1) % AVERAGE_VALUES;
    if (count < AVERAGE_VALUES) count++;

    float avg = (float)(sum / count);

    return (int)roundf(avg * 10.0f);
}

void reset_moving_average(void) {
    for (int i = 0; i < AVERAGE_VALUES; i++) values[i] = 0;
    index_ = 0;
    count = 0;
    sum = 0;
}

int process_scale_value(const uint8_t *byte_data) {
    // byte 0, modifier -40 (Nissan specific)
    int byte_pos = 0;
    return byte_data[byte_pos] - 40;
}
