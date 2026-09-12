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

void test_process_scale_value_applies_nissan_minus_40_offset(void) {
    uint8_t data[8] = {40, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(0, process_scale_value(data));
}

void test_process_scale_value_can_go_negative(void) {
    uint8_t data[8] = {10, 0, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-30, process_scale_value(data));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_moving_average_of_constant_value_converges_to_that_value_x10);
    RUN_TEST(test_moving_average_first_reading_has_no_history_to_average_against);
    RUN_TEST(test_moving_average_window_slides_after_ten_samples);
    RUN_TEST(test_process_scale_value_applies_nissan_minus_40_offset);
    RUN_TEST(test_process_scale_value_can_go_negative);
    return UNITY_END();
}
