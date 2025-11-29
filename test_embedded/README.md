# OpenEVSE ESP32 WiFi Unit Tests

This directory contains comprehensive unit tests for the OpenEVSE ESP32 WiFi firmware using the PlatformIO Unity test framework.

## Test Structure

### Test Files

- **test_main.cpp** - Main test runner that coordinates all test suites
- **test_input_filter.cpp** - Tests for the InputFilter class (exponential smoothing)
- **test_evse_state.cpp** - Tests for the EvseState enum class
- **test_divert_mode.cpp** - Tests for the DivertMode enum class
- **test_limit.cpp** - Tests for LimitType and LimitProperties classes
- **test_energy_meter.cpp** - Tests for EnergyMeterData structure and serialization
- **test_app_config.cpp** - Tests for configuration system inline functions and flags
- **test_integration.cpp** - Integration tests for component interactions

### Test Coverage

#### InputFilter Class

- Mathematical correctness of exponential smoothing
- Edge cases (zero tau, minimum tau)
- Step response and stability
- Time-dependent filtering behavior
- Multiple filter instances

#### EvseState Enum

- String parsing (fromString/toString)
- Enum value conversions
- Operator overloading
- Type safety
- Roundtrip conversions

#### DivertMode Enum

- Value assignments and conversions
- Long integer compatibility
- Operator functionality
- Type safety features

#### Limit System

- LimitType string conversions
- LimitProperties serialization/deserialization
- JSON data validation
- Error handling for invalid data

#### Energy Meter

- EnergyMeterData structure operations
- Reset functionality (partial/full)
- JSON serialization roundtrips
- Boundary value handling
- Date structure operations

#### Configuration System

- Flag bit operations
- Multi-bit field extraction
- Service enable/disable functions
- Flag isolation and interaction
- Configuration persistence simulation

#### Integration Tests

- Component interaction scenarios
- Configuration persistence simulation
- Error handling across components
- Real-world usage patterns

## Running Tests

### Prerequisites

1. Install PlatformIO Core or use PlatformIO IDE
2. Ensure all project dependencies are installed:

   ```bash
   pio lib install
   ```

### Test Environments

The platformio.ini file defines several test environments:

#### Native Testing (Recommended for Development)

```bash
pio test -e native_test
```

Runs tests on the host machine without ESP32 hardware. Fast execution for development.

#### ESP32 Hardware Testing

```bash
pio test -e esp32_test
```

Runs tests on actual ESP32 hardware. Required for hardware-specific behavior validation.

#### Specific Board Testing

```bash
pio test -e test_openevse_wifi_v1
```

Tests on the specific OpenEVSE WiFi v1 board configuration.

#### Generic ESP32 Testing

```bash
pio test -e test_esp32dev
```

Tests on a generic ESP32 development board.

### Running Specific Tests

To run only specific test files:

```bash
pio test -e native_test --filter test_input_filter
pio test -e esp32_test --filter test_integration
```

To run all tests:

```bash
pio test
```

## Test Design Patterns

### Mocking

- **Time Mocking**: Tests use mock `millis()` function for deterministic time-dependent behavior
- **Hardware Abstraction**: Tests avoid direct hardware dependencies where possible

### Test Organization

- **Arrange-Act-Assert**: Tests follow clear setup, execution, and verification phases
- **Edge Case Coverage**: Tests include boundary conditions and error cases
- **Integration Scenarios**: Higher-level tests verify component interactions

### Assertions

- **Floating Point**: Uses `TEST_ASSERT_DOUBLE_WITHIN()` for floating-point comparisons
- **Enums**: Tests enum values directly and through conversions
- **Strings**: Validates string operations with exact matching
- **JSON**: Verifies serialization/deserialization correctness

## Adding New Tests

### For New Components

1. Create a new test file: `test_component_name.cpp`
2. Include necessary headers and the component under test
3. Implement `setUp()` and `tearDown()` functions
4. Write individual test functions with descriptive names
5. Create a `run_component_tests()` function
6. Add the test runner to `test_main.cpp`

### Test Function Naming Convention

```cpp
void test_component_specific_behavior(void) {
    // Test implementation
}
```

Use descriptive names that explain what is being tested.

### Example Test Structure

```cpp
#include <unity.h>
#include "component.h"

void setUp(void) {
    // Initialize before each test
}

void tearDown(void) {
    // Clean up after each test
}

void test_component_basic_functionality(void) {
    // Arrange
    Component comp;

    // Act
    bool result = comp.doSomething();

    // Assert
    TEST_ASSERT_TRUE(result);
}

void run_component_tests(void) {
    RUN_TEST(test_component_basic_functionality);
}
```

## Debugging Tests

### Serial Output

Tests running on ESP32 hardware will output results to the serial monitor:

```bash
pio test -e esp32_test --monitor
```

### Verbose Mode

For detailed test execution information:

```bash
pio test -e native_test -v
```

### Test Debugging

Use debug builds for detailed information:

```bash
pio test -e esp32_test --debug
```

## Continuous Integration

These tests are designed to run in CI/CD environments:

- **Native tests** run quickly without hardware requirements
- **ESP32 tests** can run on hardware-in-the-loop CI setups
- **JSON output** available for CI result parsing

## Test Maintenance

### When Adding New Features

1. Add unit tests for new classes/functions
2. Add integration tests for component interactions
3. Update existing tests if interfaces change
4. Ensure backward compatibility tests pass

### When Fixing Bugs

1. Add regression tests that reproduce the bug
2. Verify the fix resolves the test
3. Ensure existing tests still pass

### Performance Considerations

- Native tests run in seconds
- ESP32 tests may take several minutes
- Use native tests for rapid development cycles
- Use ESP32 tests for final validation

## Dependencies

Tests depend on the same libraries as the main firmware:

- ArduinoJson for JSON handling
- Unity test framework (built into PlatformIO)
- ESP32 Arduino framework for embedded tests
- All project library dependencies

## Known Limitations

1. **Hardware Dependencies**: Some components require actual ESP32 hardware for complete testing
2. **Timing Dependencies**: Real-time behavior may vary between native and embedded tests
3. **Memory Constraints**: ESP32 tests must fit within device memory limits
4. **Floating Point**: Precision may vary between platforms

## Contributing

When contributing new tests:

1. Follow existing naming conventions
2. Include both positive and negative test cases
3. Test edge cases and error conditions
4. Add integration tests for component interactions
5. Ensure tests run on both native and ESP32 environments
6. Update this documentation for new test patterns
