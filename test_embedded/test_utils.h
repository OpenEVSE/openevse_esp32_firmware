#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <unity.h>

// Mock time variable for deterministic testing
extern unsigned long mock_millis_value;

// Mock time management functions
void set_mock_millis(unsigned long value);
void advance_mock_millis(unsigned long increment);
unsigned long get_mock_millis();

// Global Unity setUp and tearDown functions - will be called automatically
void setUp(void);
void tearDown(void);

// Common setUp and tearDown functions
// These are declared inline to avoid multiple definitions
inline void common_setUp(void) {
    // Reset mock time for each test
    set_mock_millis(0);
}

inline void common_tearDown(void) {
    // Nothing to clean up
}

#endif // TEST_UTILS_H