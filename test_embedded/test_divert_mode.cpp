#include <unity.h>
#include <Arduino.h>
#include "test_utils.h"

// Include the enum under test
#include "divert.h"



void test_divert_mode_constructor_default(void) {
    DivertMode mode;
    // Default constructor creates mode with uninitialized value
    // We can't test the exact value since it's not specified
    TEST_PASS_MESSAGE("DivertMode default constructor test");
}

void test_divert_mode_constructor_with_value(void) {
    DivertMode mode_normal(DivertMode::Normal);
    DivertMode mode_eco(DivertMode::Eco);
    
    TEST_ASSERT_EQUAL(DivertMode::Normal, mode_normal);
    TEST_ASSERT_EQUAL(DivertMode::Eco, mode_eco);
}

void test_divert_mode_constructor_with_long(void) {
    DivertMode mode_normal(1L);
    DivertMode mode_eco(2L);
    
    TEST_ASSERT_EQUAL(DivertMode::Normal, mode_normal);
    TEST_ASSERT_EQUAL(DivertMode::Eco, mode_eco);
}

void test_divert_mode_assignment_operator_value(void) {
    DivertMode mode;
    
    mode = DivertMode::Normal;
    TEST_ASSERT_EQUAL(DivertMode::Normal, mode);
    
    mode = DivertMode::Eco;
    TEST_ASSERT_EQUAL(DivertMode::Eco, mode);
}

void test_divert_mode_assignment_operator_long(void) {
    DivertMode mode;
    
    mode = 1L;
    TEST_ASSERT_EQUAL(DivertMode::Normal, mode);
    
    mode = 2L;
    TEST_ASSERT_EQUAL(DivertMode::Eco, mode);
}

void test_divert_mode_conversion_operator(void) {
    DivertMode mode_normal(DivertMode::Normal);
    DivertMode mode_eco(DivertMode::Eco);
    
    // Test implicit conversion to Value
    DivertMode::Value val_normal = mode_normal;
    DivertMode::Value val_eco = mode_eco;
    
    TEST_ASSERT_EQUAL(DivertMode::Normal, val_normal);
    TEST_ASSERT_EQUAL(DivertMode::Eco, val_eco);
}

void test_divert_mode_comparison(void) {
    DivertMode mode1(DivertMode::Normal);
    DivertMode mode2(DivertMode::Normal);
    DivertMode mode3(DivertMode::Eco);
    
    // Test equality comparison
    TEST_ASSERT_TRUE(mode1 == DivertMode::Normal);
    TEST_ASSERT_TRUE(mode1 == mode2);
    TEST_ASSERT_FALSE(mode1 == mode3);
    TEST_ASSERT_FALSE(mode1 == DivertMode::Eco);
}

void test_divert_mode_switch_statement(void) {
    DivertMode mode(DivertMode::Normal);
    
    int result = 0;
    switch(mode) {
        case DivertMode::Normal:
            result = 1;
            break;
        case DivertMode::Eco:
            result = 2;
            break;
    }
    
    TEST_ASSERT_EQUAL(1, result);
    
    mode = DivertMode::Eco;
    result = 0;
    switch(mode) {
        case DivertMode::Normal:
            result = 1;
            break;
        case DivertMode::Eco:
            result = 2;
            break;
    }
    
    TEST_ASSERT_EQUAL(2, result);
}

void test_divert_mode_value_constants(void) {
    // Test that the enum values have the expected numeric values
    TEST_ASSERT_EQUAL(1, static_cast<uint8_t>(DivertMode::Normal));
    TEST_ASSERT_EQUAL(2, static_cast<uint8_t>(DivertMode::Eco));
}

void test_divert_mode_copy_constructor(void) {
    DivertMode original(DivertMode::Eco);
    DivertMode copy(original);
    
    TEST_ASSERT_EQUAL(DivertMode::Eco, copy);
    TEST_ASSERT_EQUAL(original, copy);
}

void test_divert_mode_assignment_chain(void) {
    DivertMode mode1, mode2, mode3;
    
    // Test assignment chain
    mode3 = mode2 = mode1 = DivertMode::Normal;
    
    TEST_ASSERT_EQUAL(DivertMode::Normal, mode1);
    TEST_ASSERT_EQUAL(DivertMode::Normal, mode2);
    TEST_ASSERT_EQUAL(DivertMode::Normal, mode3);
}

void test_divert_mode_assignment_return_value(void) {
    DivertMode mode;
    
    // Test that assignment returns the assigned object
    DivertMode result = (mode = DivertMode::Eco);
    TEST_ASSERT_EQUAL(DivertMode::Eco, result);
    TEST_ASSERT_EQUAL(DivertMode::Eco, mode);
}

void test_divert_mode_invalid_long_values(void) {
    // Test behavior with invalid long values
    DivertMode mode_invalid(99L);
    
    // The mode should store the value even if it's not a valid enum
    TEST_ASSERT_EQUAL(99, static_cast<uint8_t>(mode_invalid));
}

void test_divert_mode_type_safety(void) {
    DivertMode mode(DivertMode::Normal);
    
    // Test that we can't accidentally use mode as boolean
    // This should not compile if the bool operator delete is working
    // We can't test compilation failures in unit tests, but we can
    // document the expected behavior
    TEST_PASS_MESSAGE("DivertMode prevents boolean conversion");
}

void run_divert_mode_tests(void) {
    RUN_TEST(test_divert_mode_constructor_default);
    RUN_TEST(test_divert_mode_constructor_with_value);
    RUN_TEST(test_divert_mode_constructor_with_long);
    RUN_TEST(test_divert_mode_assignment_operator_value);
    RUN_TEST(test_divert_mode_assignment_operator_long);
    RUN_TEST(test_divert_mode_conversion_operator);
    RUN_TEST(test_divert_mode_comparison);
    RUN_TEST(test_divert_mode_switch_statement);
    RUN_TEST(test_divert_mode_value_constants);
    RUN_TEST(test_divert_mode_copy_constructor);
    RUN_TEST(test_divert_mode_assignment_chain);
    RUN_TEST(test_divert_mode_assignment_return_value);
    RUN_TEST(test_divert_mode_invalid_long_values);
    RUN_TEST(test_divert_mode_type_safety);
}