#include <unity.h>
#include <Arduino.h>
#include "test_utils.h"

// Include the enum under test
#include "evse_state.h"



void test_evse_state_constructor_default(void) {
    EvseState state;
    // Default constructor should create None state
    TEST_ASSERT_EQUAL(EvseState::None, state);
}

void test_evse_state_constructor_with_value(void) {
    EvseState state_active(EvseState::Active);
    EvseState state_disabled(EvseState::Disabled);
    EvseState state_none(EvseState::None);

    TEST_ASSERT_EQUAL(EvseState::Active, state_active);
    TEST_ASSERT_EQUAL(EvseState::Disabled, state_disabled);
    TEST_ASSERT_EQUAL(EvseState::None, state_none);
}

void test_evse_state_from_string_active(void) {
    EvseState state;

    // Test various forms of "active"
    TEST_ASSERT_TRUE(state.fromString("active"));
    TEST_ASSERT_EQUAL(EvseState::Active, state);

    TEST_ASSERT_TRUE(state.fromString("a"));
    TEST_ASSERT_EQUAL(EvseState::Active, state);

    TEST_ASSERT_TRUE(state.fromString("activate"));
    TEST_ASSERT_EQUAL(EvseState::Active, state);
}

void test_evse_state_from_string_disabled(void) {
    EvseState state;

    // Test various forms of "disabled"
    TEST_ASSERT_TRUE(state.fromString("disabled"));
    TEST_ASSERT_EQUAL(EvseState::Disabled, state);

    TEST_ASSERT_TRUE(state.fromString("d"));
    TEST_ASSERT_EQUAL(EvseState::Disabled, state);

    TEST_ASSERT_TRUE(state.fromString("disable"));
    TEST_ASSERT_EQUAL(EvseState::Disabled, state);
}

void test_evse_state_from_string_invalid(void) {
    EvseState state;

    // Test invalid strings
    TEST_ASSERT_FALSE(state.fromString("invalid"));
    TEST_ASSERT_FALSE(state.fromString("none"));
    TEST_ASSERT_FALSE(state.fromString(""));
    TEST_ASSERT_FALSE(state.fromString("x"));
    TEST_ASSERT_FALSE(state.fromString("123"));
}

void test_evse_state_from_string_case_sensitivity(void) {
    EvseState state;

    // Test that it only checks first character and is case sensitive
    TEST_ASSERT_TRUE(state.fromString("active"));
    TEST_ASSERT_FALSE(state.fromString("Active")); // Capital A should fail
    TEST_ASSERT_FALSE(state.fromString("DISABLED")); // Capital D should fail
}

void test_evse_state_to_string_active(void) {
    EvseState state(EvseState::Active);
    const char* str = state.toString();
    TEST_ASSERT_EQUAL_STRING("active", str);
}

void test_evse_state_to_string_disabled(void) {
    EvseState state(EvseState::Disabled);
    const char* str = state.toString();
    TEST_ASSERT_EQUAL_STRING("disabled", str);
}

void test_evse_state_to_string_none(void) {
    EvseState state(EvseState::None);
    const char* str = state.toString();
    TEST_ASSERT_EQUAL_STRING("none", str);
}

void test_evse_state_to_string_unknown(void) {
    EvseState state;
    // Force an invalid state value
    state = (EvseState::Value)99;
    const char* str = state.toString();
    TEST_ASSERT_EQUAL_STRING("unknown", str);
}

void test_evse_state_assignment_operator(void) {
    EvseState state;

    // Test assignment with Value enum
    state = EvseState::Active;
    TEST_ASSERT_EQUAL(EvseState::Active, state);

    state = EvseState::Disabled;
    TEST_ASSERT_EQUAL(EvseState::Disabled, state);

    state = EvseState::None;
    TEST_ASSERT_EQUAL(EvseState::None, state);
}

void test_evse_state_conversion_operator(void) {
    EvseState state_active(EvseState::Active);
    EvseState state_disabled(EvseState::Disabled);
    EvseState state_none(EvseState::None);

    // Test implicit conversion to Value
    EvseState::Value val_active = state_active;
    EvseState::Value val_disabled = state_disabled;
    EvseState::Value val_none = state_none;

    TEST_ASSERT_EQUAL(EvseState::Active, val_active);
    TEST_ASSERT_EQUAL(EvseState::Disabled, val_disabled);
    TEST_ASSERT_EQUAL(EvseState::None, val_none);
}

void test_evse_state_comparison(void) {
    EvseState state1(EvseState::Active);
    EvseState state2(EvseState::Active);
    EvseState state3(EvseState::Disabled);

    // Test equality comparison
    TEST_ASSERT_TRUE(state1 == EvseState::Active);
    TEST_ASSERT_TRUE(state1 == state2);
    TEST_ASSERT_FALSE(state1 == state3);
    TEST_ASSERT_FALSE(state1 == EvseState::Disabled);
}

void test_evse_state_switch_statement(void) {
    EvseState state(EvseState::Active);

    int result = 0;
    switch(state) {
        case EvseState::Active:
            result = 1;
            break;
        case EvseState::Disabled:
            result = 2;
            break;
        case EvseState::None:
            result = 3;
            break;
    }

    TEST_ASSERT_EQUAL(1, result);
}

void test_evse_state_roundtrip_conversion(void) {
    // Test that fromString and toString are consistent
    EvseState state;

    // Active roundtrip
    TEST_ASSERT_TRUE(state.fromString("active"));
    TEST_ASSERT_EQUAL_STRING("active", state.toString());

    // Disabled roundtrip
    TEST_ASSERT_TRUE(state.fromString("disabled"));
    TEST_ASSERT_EQUAL_STRING("disabled", state.toString());
}

void test_evse_state_copy_constructor(void) {
    EvseState original(EvseState::Active);
    EvseState copy(original);

    TEST_ASSERT_EQUAL(EvseState::Active, copy);
    TEST_ASSERT_EQUAL(original, copy);
}

void run_evse_state_tests(void) {
    RUN_TEST(test_evse_state_constructor_default);
    RUN_TEST(test_evse_state_constructor_with_value);
    RUN_TEST(test_evse_state_from_string_active);
    RUN_TEST(test_evse_state_from_string_disabled);
    RUN_TEST(test_evse_state_from_string_invalid);
    RUN_TEST(test_evse_state_from_string_case_sensitivity);
    RUN_TEST(test_evse_state_to_string_active);
    RUN_TEST(test_evse_state_to_string_disabled);
    RUN_TEST(test_evse_state_to_string_none);
    RUN_TEST(test_evse_state_to_string_unknown);
    RUN_TEST(test_evse_state_assignment_operator);
    RUN_TEST(test_evse_state_conversion_operator);
    RUN_TEST(test_evse_state_comparison);
    RUN_TEST(test_evse_state_switch_statement);
    RUN_TEST(test_evse_state_roundtrip_conversion);
    RUN_TEST(test_evse_state_copy_constructor);
}
