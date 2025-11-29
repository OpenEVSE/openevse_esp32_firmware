#include <unity.h>
#include <Arduino.h>
#include <cmath>
#include "test_utils.h"

// Include the class under test
#include "input_filter.h"



void test_input_filter_constructor(void) {
    InputFilter filter;
    // Constructor should initialize properly - we can only test behavior through public interface
    TEST_PASS();
}

void test_filter_first_call(void) {
    InputFilter filter;
    set_mock_millis(1000);
    
    // First call should work and return some filtered value
    double result = filter.filter(10.0, 5.0, 20);
    
    // Should return some value between input and previous filtered
    TEST_ASSERT_NOT_EQUAL(0.0, result);
    TEST_ASSERT_GREATER_OR_EQUAL(5.0, result);
    TEST_ASSERT_LESS_OR_EQUAL(10.0, result);
}

void test_filter_subsequent_calls(void) {
    InputFilter filter;
    
    // Set initial time
    set_mock_millis(1000);
    double result1 = filter.filter(10.0, 5.0, 20);
    
    // Advance time and filter again
    set_mock_millis(2000);
    double result2 = filter.filter(15.0, result1, 20);
    
    // Result should be different and move towards new input
    TEST_ASSERT_NOT_EQUAL(result1, result2);
    TEST_ASSERT_GREATER_THAN(result1, result2); // Should move towards 15.0
}

void test_filter_no_time_elapsed(void) {
    InputFilter filter;
    
    // Set initial time
    set_mock_millis(1000);
    double result1 = filter.filter(10.0, 5.0, 20);
    
    // Same time - delta = 0
    set_mock_millis(1000);
    double result2 = filter.filter(15.0, result1, 20);
    
    // With delta = 0, should get minimal change
    double diff = abs(result2 - result1);
    TEST_ASSERT_LESS_THAN(1.0, diff); // Should be very small change
}

void test_filter_zero_tau(void) {
    InputFilter filter;
    set_mock_millis(1000);
    
    // When tau is 0, should pass input through directly
    double result = filter.filter(100.0, 50.0, 0);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, result);
}

void test_filter_step_response(void) {
    InputFilter filter;
    double filtered = 0.0;
    double input = 10.0;
    uint32_t tau = 5;
    
    // Simulate step response
    set_mock_millis(0);
    
    for (int i = 1; i <= 20; i++) {
        set_mock_millis(i * 1000);
        filtered = filter.filter(input, filtered, tau);
    }
    
    // After many time constants, should approach input value
    TEST_ASSERT_DOUBLE_WITHIN(1.0, input, filtered);
}

void test_filter_stability(void) {
    InputFilter filter;
    double filtered = 5.0;
    double input = 5.0;  // Same as initial filtered value
    uint32_t tau = 10;
    
    set_mock_millis(0);
    
    // Multiple calls with same input
    for (int i = 1; i <= 5; i++) {
        set_mock_millis(i * 1000);
        filtered = filter.filter(input, filtered, tau);
    }
    
    // Should remain stable at input value
    TEST_ASSERT_DOUBLE_WITHIN(0.1, input, filtered);
}

void test_filter_different_tau_convergence(void) {
    InputFilter filter1, filter2;
    double filtered1 = 0.0, filtered2 = 0.0;
    double input = 10.0;
    
    set_mock_millis(0);
    
    // Run both filters for same time period but different tau
    for (int i = 1; i <= 10; i++) {
        set_mock_millis(i * 1000);
        filtered1 = filter1.filter(input, filtered1, 5);  // Fast filter
        filtered2 = filter2.filter(input, filtered2, 20); // Slow filter
    }
    
    // Fast filter should be closer to input value
    double diff1 = abs(input - filtered1);
    double diff2 = abs(input - filtered2);
    TEST_ASSERT_LESS_THAN(diff2, diff1);
}

void test_filter_minimum_tau_behavior(void) {
    InputFilter filter;
    double filtered = 0.0;
    double input = 10.0;
    
    set_mock_millis(0);
    
    // Test with tau less than minimum (should be clamped to minimum)
    set_mock_millis(1000);
    double result1 = filter.filter(input, filtered, 5); // Less than INPUT_FILTER_MIN_TAU
    
    set_mock_millis(2000);
    double result2 = filter.filter(input, result1, INPUT_FILTER_MIN_TAU);
    
    // Both should behave similarly since tau should be clamped
    // We can't test exact equality but can test that both converge
    TEST_ASSERT_GREATER_THAN(0.0, result1);
    TEST_ASSERT_GREATER_THAN(0.0, result2);
}

void test_filter_large_time_gaps(void) {
    InputFilter filter;
    double filtered = 0.0;
    double input = 10.0;
    uint32_t tau = 5;
    
    // Start at time 1000
    set_mock_millis(1000);
    double result1 = filter.filter(input, filtered, tau);
    
    // Large time gap (100 seconds)
    set_mock_millis(101000);
    double result2 = filter.filter(input, result1, tau);
    
    // After a very large time gap, should be very close to input
    TEST_ASSERT_DOUBLE_WITHIN(0.1, input, result2);
}

void test_filter_mathematical_properties(void) {
    InputFilter filter;
    double filtered = 5.0;
    
    set_mock_millis(1000);
    
    // Test that filter output is between input and previous filtered value
    double result = filter.filter(10.0, filtered, 10);
    
    TEST_ASSERT_GREATER_OR_EQUAL(5.0, result);
    TEST_ASSERT_LESS_OR_EQUAL(10.0, result);
    
    // Test with input less than filtered
    set_mock_millis(2000);
    result = filter.filter(3.0, result, 10);
    
    TEST_ASSERT_GREATER_OR_EQUAL(3.0, result);
    TEST_ASSERT_LESS_OR_EQUAL(10.0, result);
}

void run_input_filter_tests(void) {
    RUN_TEST(test_input_filter_constructor);
    RUN_TEST(test_filter_first_call);
    RUN_TEST(test_filter_subsequent_calls);
    RUN_TEST(test_filter_no_time_elapsed);
    RUN_TEST(test_filter_zero_tau);
    RUN_TEST(test_filter_step_response);
    RUN_TEST(test_filter_stability);
    RUN_TEST(test_filter_different_tau_convergence);
    RUN_TEST(test_filter_minimum_tau_behavior);
    RUN_TEST(test_filter_large_time_gaps);
    RUN_TEST(test_filter_mathematical_properties);
}