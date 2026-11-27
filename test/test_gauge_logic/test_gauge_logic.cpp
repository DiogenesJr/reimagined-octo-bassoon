#include <unity.h>
#include "GaugeLogic.h"

void setUp(void) {
    reset_moving_average();
}

void tearDown(void) {}

void test_moving_average_of_constant_value_converges_to_that_value_x10(void) {
    int result = 0;
    for (int i = 0; i < 20; i++) {
        result = get_moving_average(50);
    }
    TEST_ASSERT_EQUAL_INT(500, result);
}

void test_moving_average_first_reading_has_no_history_to_average_against(void) {
    // With only 1 sample so far, count is 1, so avg == the sample itself: 100 x10 -> 1000
    int first = get_moving_average(100);
    TEST_ASSERT_EQUAL_INT(1000, first);
}

void test_moving_average_window_slides_after_ten_samples(void) {
    for (int i = 0; i < 10; i++) {
        get_moving_average(10);
    }
    // Window is now full of 10s (avg 10 -> 100). A new value of 20 replaces
    // the oldest 10: avg = (9*10 + 20) / 10 = 11 -> 110.
    int result = get_moving_average(20);
    TEST_ASSERT_EQUAL_INT(110, result);
}

// first 0x345 frame of a log recorded from the car, at a cold start
static const uint8_t cold_start_0x345[8] = {0x62, 0x0F, 0xC6, 0x41, 0x42, 0x32, 0xD7, 0xB9};

void test_process_oil_temp_value_reads_byte_three(void) {
    TEST_ASSERT_EQUAL_INT(25, process_oil_temp_value(cold_start_0x345));
}

void test_process_coolant_temp_value_reads_byte_four(void) {
    TEST_ASSERT_EQUAL_INT(26, process_coolant_temp_value(cold_start_0x345));
}

void test_process_temperature_values_can_go_negative(void) {
    uint8_t data[8] = {0, 0, 0, 10, 20, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-30, process_oil_temp_value(data));
    TEST_ASSERT_EQUAL_INT(-20, process_coolant_temp_value(data));
}

void test_process_oil_pressure_value_reads_byte_zero_unsigned(void) {
    uint8_t data[8] = {200, 99, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(200, process_oil_pressure_value(data));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_moving_average_of_constant_value_converges_to_that_value_x10);
    RUN_TEST(test_moving_average_first_reading_has_no_history_to_average_against);
    RUN_TEST(test_moving_average_window_slides_after_ten_samples);
    RUN_TEST(test_process_oil_temp_value_reads_byte_three);
    RUN_TEST(test_process_coolant_temp_value_reads_byte_four);
    RUN_TEST(test_process_temperature_values_can_go_negative);
    RUN_TEST(test_process_oil_pressure_value_reads_byte_zero_unsigned);
    return UNITY_END();
}
