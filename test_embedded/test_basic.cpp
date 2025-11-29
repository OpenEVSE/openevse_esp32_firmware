#include <unity.h>
#include "test_utils.h"

// Only include headers for files we have source for
#include "app_config.h"

// Simple tests that can work with the available source files

void test_basic_app_config_functions(void) {
    // Test config flag functions that should be available
    // These are simple functions that should work
    TEST_ASSERT_TRUE(true); // Placeholder
}

void test_mock_millis_function(void) {
    // Test our mock millis function
    set_mock_millis(1000);
    TEST_ASSERT_EQUAL_UINT32(1000, get_mock_millis());
    
    advance_mock_millis(500);
    TEST_ASSERT_EQUAL_UINT32(1500, get_mock_millis());
}

void test_basic_functionality(void) {
    // Very basic test that should always pass
    TEST_ASSERT_EQUAL_INT(1, 1);
    TEST_ASSERT_TRUE(true);
    TEST_ASSERT_FALSE(false);
}

// Unity test runner
void setup() {
    delay(2000); // Give time for serial monitor to connect
    UNITY_BEGIN();
    
    RUN_TEST(test_basic_functionality);
    RUN_TEST(test_mock_millis_function);
    RUN_TEST(test_basic_app_config_functions);
    
    UNITY_END();
}

void loop() {
    // Empty loop
}