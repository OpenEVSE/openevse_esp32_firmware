#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "test_utils.h"

// Include components for integration testing
#include "input_filter.h"
#include "evse_state.h"
#include "limit.h"
#include "app_config.h"
#include "energy_meter.h"



// ==============================================
// Integration Tests
// ==============================================

void test_input_filter_with_config_flags(void) {
    // Test that input filter works correctly when divert is enabled via config
    flags = CONFIG_SERVICE_DIVERT;
    TEST_ASSERT_TRUE(config_divert_enabled());

    InputFilter filter;
    double filtered = 0.0;
    double input = 10.0;
    uint32_t tau = 5;

    // Simulate filtering over time
    for (int i = 1; i <= 5; i++) {
        set_mock_millis(i * 1000);
        filtered = filter.filter(input, filtered, tau);
    }

    // Should converge towards input when divert is enabled
    TEST_ASSERT_GREATER_THAN(8.0, filtered);
}

void test_limit_properties_with_evse_state_integration(void) {
    LimitProperties props;
    EvseState state;

    // Test setting up time limit when EVSE is active
    TEST_ASSERT_TRUE(state.fromString("active"));
    TEST_ASSERT_EQUAL(EvseState::Active, state);

    props.setType(LimitType::Time);
    props.setValue(3600); // 1 hour
    props.setAutoRelease(true);

    // Verify limit is properly configured
    TEST_ASSERT_EQUAL(LimitType::Time, props.getType());
    TEST_ASSERT_EQUAL(3600, props.getValue());
    TEST_ASSERT_TRUE(props.getAutoRelease());

    // Test JSON serialization/deserialization
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    props.serialize(obj);

    LimitProperties props2;
    TEST_ASSERT_TRUE(props2.deserialize(obj));

    TEST_ASSERT_EQUAL(props.getType(), props2.getType());
    TEST_ASSERT_EQUAL(props.getValue(), props2.getValue());
    TEST_ASSERT_EQUAL(props.getAutoRelease(), props2.getAutoRelease());
}

void test_energy_meter_with_configuration_integration(void) {
    // Enable energy meter via configuration
    flags = CONFIG_SERVICE_EMONCMS;
    TEST_ASSERT_TRUE(config_emoncms_enabled());

    EnergyMeterData data;

    // Simulate charging session
    data.session = 25.5;  // 25.5 kWh session
    data.total = 1000.0;  // 1000 kWh total
    data.switches = 50;   // 50 relay switches
    data.elapsed = 3600;  // 1 hour elapsed

    // Test serialization
    StaticJsonDocument<capacity> doc;
    data.serialize(doc);

    // Verify data is preserved
    TEST_ASSERT_EQUAL_DOUBLE(25.5, doc["session"]);
    TEST_ASSERT_EQUAL_DOUBLE(1000.0, doc["total"]);
    TEST_ASSERT_EQUAL(50, doc["switches"]);
    TEST_ASSERT_EQUAL_DOUBLE(3600, doc["elapsed"]);
}

void test_multiple_services_enabled(void) {
    // Enable multiple services
    flags = CONFIG_SERVICE_MQTT | CONFIG_SERVICE_DIVERT | CONFIG_RFID;

    TEST_ASSERT_TRUE(config_mqtt_enabled());
    TEST_ASSERT_TRUE(config_divert_enabled());
    TEST_ASSERT_TRUE(config_rfid_enabled());
    TEST_ASSERT_FALSE(config_emoncms_enabled());
    TEST_ASSERT_FALSE(config_ocpp_enabled());

    // Test that limit system works with these services
    LimitProperties props;
    props.setType(LimitType::Energy);
    props.setValue(50); // 50 kWh limit

    // Serialize to simulate saving configuration
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    props.serialize(obj);

    TEST_ASSERT_EQUAL_STRING("energy", obj["type"]);
    TEST_ASSERT_EQUAL(50, obj["value"]);
}

void test_evse_state_transitions_with_limits(void) {
    EvseState state;
    LimitProperties limit_props;

    // Start in disabled state
    TEST_ASSERT_TRUE(state.fromString("disabled"));
    TEST_ASSERT_EQUAL(EvseState::Disabled, state);

    // Set up energy limit
    limit_props.setType(LimitType::Energy);
    limit_props.setValue(100); // 100 kWh
    limit_props.setAutoRelease(true);

    // Activate EVSE
    TEST_ASSERT_TRUE(state.fromString("active"));
    TEST_ASSERT_EQUAL(EvseState::Active, state);

    // Verify limit is still valid
    TEST_ASSERT_EQUAL(LimitType::Energy, limit_props.getType());
    TEST_ASSERT_EQUAL(100, limit_props.getValue());
    TEST_ASSERT_TRUE(limit_props.getAutoRelease());

    // Test state string conversion
    TEST_ASSERT_EQUAL_STRING("active", state.toString());
}

void test_filter_with_energy_meter_data(void) {
    InputFilter power_filter, current_filter;
    EnergyMeterData meter_data;

    // Simulate varying power readings that need filtering
    double power_readings[] = {1000, 1200, 950, 1100, 1050, 1000};
    double current_readings[] = {4.5, 5.0, 4.2, 4.8, 4.6, 4.4};
    int num_readings = sizeof(power_readings) / sizeof(power_readings[0]);

    double filtered_power = 0.0;
    double filtered_current = 0.0;
    uint32_t tau = 10; // 10 second time constant

    // Process readings
    for (int i = 0; i < num_readings; i++) {
        set_mock_millis((i + 1) * 2000); // 2 second intervals

        filtered_power = power_filter.filter(power_readings[i], filtered_power, tau);
        filtered_current = current_filter.filter(current_readings[i], filtered_current, tau);

        // Update meter data (simplified)
        meter_data.session += power_readings[i] * 2.0 / 3600.0; // Convert to kWh
        meter_data.elapsed += 2.0; // 2 seconds per reading
    }

    // Verify filtering worked
    TEST_ASSERT_GREATER_THAN(900.0, filtered_power);
    TEST_ASSERT_LESS_THAN(1300.0, filtered_power);

    TEST_ASSERT_GREATER_THAN(4.0, filtered_current);
    TEST_ASSERT_LESS_THAN(5.5, filtered_current);

    // Verify meter accumulated data
    TEST_ASSERT_GREATER_THAN(0.0, meter_data.session);
    TEST_ASSERT_EQUAL_DOUBLE(12.0, meter_data.elapsed);
}

void test_configuration_persistence_simulation(void) {
    // Simulate saving and loading configuration
    flags = CONFIG_SERVICE_MQTT | CONFIG_SERVICE_DIVERT | (3 << 10); // Charge mode 3

    // Create limit configuration
    LimitProperties limit_config;
    limit_config.setType(LimitType::Time);
    limit_config.setValue(7200); // 2 hours
    limit_config.setAutoRelease(false);

    // Create energy meter data
    EnergyMeterData meter_data;
    meter_data.total = 5000.0;
    meter_data.switches = 200;
    meter_data.imported = true;

    // Serialize all data (simulating save to flash)
    DynamicJsonDocument config_doc(1024);
    JsonObject config_obj = config_doc.to<JsonObject>();
    config_obj["flags"] = flags;

    JsonObject limit_obj = config_obj.createNestedObject("limit");
    limit_config.serialize(limit_obj);

    StaticJsonDocument<capacity> meter_doc;
    meter_data.serialize(meter_doc);
    config_obj["energy_meter"] = meter_doc.as<JsonObject>();

    // Simulate restart - clear everything
    flags = 0;
    LimitProperties restored_limit;
    EnergyMeterData restored_meter;

    // Restore from "saved" data
    flags = config_obj["flags"];
    restored_limit.deserialize(limit_obj);

    JsonObject saved_meter = config_obj["energy_meter"];
    StaticJsonDocument<capacity> meter_restore_doc;
    meter_restore_doc.set(saved_meter);
    restored_meter.deserialize(meter_restore_doc);

    // Verify restoration
    TEST_ASSERT_TRUE(config_mqtt_enabled());
    TEST_ASSERT_TRUE(config_divert_enabled());
    TEST_ASSERT_EQUAL(3, config_charge_mode());

    TEST_ASSERT_EQUAL(LimitType::Time, restored_limit.getType());
    TEST_ASSERT_EQUAL(7200, restored_limit.getValue());
    TEST_ASSERT_FALSE(restored_limit.getAutoRelease());

    TEST_ASSERT_EQUAL_DOUBLE(5000.0, restored_meter.total);
    TEST_ASSERT_EQUAL(200, restored_meter.switches);
    TEST_ASSERT_TRUE(restored_meter.imported);
}

void test_error_handling_integration(void) {
    // Test graceful handling of invalid configurations
    LimitProperties props;

    // Try to deserialize invalid JSON
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    obj["type"] = "invalid_type";
    obj["value"] = -1; // Invalid negative value

    // Should handle gracefully
    bool result = props.deserialize(obj);
    TEST_ASSERT_FALSE(result); // Should fail validation

    // Test EvseState with invalid input
    EvseState state;
    TEST_ASSERT_FALSE(state.fromString("invalid_state"));

    // Test InputFilter with edge cases
    InputFilter filter;
    double filtered = filter.filter(1000.0, 0.0, 0); // tau = 0
    TEST_ASSERT_EQUAL_DOUBLE(1000.0, filtered); // Should pass through
}

void run_integration_tests(void) {
    RUN_TEST(test_input_filter_with_config_flags);
    RUN_TEST(test_limit_properties_with_evse_state_integration);
    RUN_TEST(test_energy_meter_with_configuration_integration);
    RUN_TEST(test_multiple_services_enabled);
    RUN_TEST(test_evse_state_transitions_with_limits);
    RUN_TEST(test_filter_with_energy_meter_data);
    RUN_TEST(test_configuration_persistence_simulation);
    RUN_TEST(test_error_handling_integration);
}
