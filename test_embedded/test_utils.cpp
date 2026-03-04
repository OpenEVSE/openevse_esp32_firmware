#include "test_utils.h"

#include "test_utils.h"

// Mock millis implementation
unsigned long mock_millis_value = 0;

void set_mock_millis(unsigned long value) {
    mock_millis_value = value;
}

void advance_mock_millis(unsigned long increment) {
    mock_millis_value += increment;
}

unsigned long get_mock_millis() {
    return mock_millis_value;
}

// Wrapped millis function (using linker wrap)
extern "C" unsigned long __wrap_millis() {
    return mock_millis_value;
}

// Global Unity setUp and tearDown functions
void setUp(void) {
    common_setUp();
}

void tearDown(void) {
    common_tearDown();
}