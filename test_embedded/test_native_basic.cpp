#include <unity.h>
#include <iostream>
#include <cstring>
#include <cstdlib>

// Basic native tests that don't require Arduino
void test_basic_math() {
    TEST_ASSERT_EQUAL(4, 2 + 2);
    TEST_ASSERT_EQUAL(10, 5 * 2);
}

void test_string_operations() {
    const char* test_str = "Hello";
    TEST_ASSERT_EQUAL_STRING("Hello", test_str);
    TEST_ASSERT_EQUAL(5, strlen(test_str));
}

void test_data_structures() {
    int array[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL(1, array[0]);
    TEST_ASSERT_EQUAL(2, array[1]);
    TEST_ASSERT_EQUAL(3, array[2]);
}

// Test native environment capabilities
void test_native_environment() {
    // Test that we can use standard library functions
    TEST_ASSERT_TRUE(true);
    TEST_ASSERT_FALSE(false);
    
    // Test memory allocation
    void* ptr = malloc(100);
    TEST_ASSERT_NOT_NULL(ptr);
    free(ptr);
}

void setUp(void) {
    // Set up native test environment
}

void tearDown(void) {
    // Clean up after tests
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_basic_math);
    RUN_TEST(test_string_operations);
    RUN_TEST(test_data_structures);
    RUN_TEST(test_native_environment);
    
    return UNITY_END();
}