#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "test_utils.h"

// Include the class under test
#include "energy_meter.h"



// ==============================================
// EnergyMeterDate Tests
// ==============================================

void test_energy_meter_date_struct(void) {
    EnergyMeterDate date;
    
    // Test that we can assign values
    date.day = 15;
    date.month = 6;
    date.year = 2023;
    
    TEST_ASSERT_EQUAL(15, date.day);
    TEST_ASSERT_EQUAL(6, date.month);
    TEST_ASSERT_EQUAL(2023, date.year);
}

void test_energy_meter_date_copy(void) {
    EnergyMeterDate date1;
    date1.day = 25;
    date1.month = 12;
    date1.year = 2023;
    
    EnergyMeterDate date2 = date1;
    
    TEST_ASSERT_EQUAL(date1.day, date2.day);
    TEST_ASSERT_EQUAL(date1.month, date2.month);
    TEST_ASSERT_EQUAL(date1.year, date2.year);
}

// ==============================================
// EnergyMeterData Tests
// ==============================================

void test_energy_meter_data_constructor(void) {
    EnergyMeterData data;
    
    // Constructor should initialize values to zero
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.session);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.total);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.daily);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.weekly);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.monthly);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.yearly);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.elapsed);
    TEST_ASSERT_EQUAL(0, data.switches);
    TEST_ASSERT_FALSE(data.imported);
}

void test_energy_meter_data_assignment(void) {
    EnergyMeterData data;
    
    // Test that we can assign values
    data.session = 15.5;
    data.total = 1000.75;
    data.daily = 25.0;
    data.weekly = 150.25;
    data.monthly = 600.0;
    data.yearly = 7200.5;
    data.elapsed = 3600.0;
    data.switches = 42;
    data.imported = true;
    
    data.date.day = 15;
    data.date.month = 6;
    data.date.year = 2023;
    
    TEST_ASSERT_EQUAL_DOUBLE(15.5, data.session);
    TEST_ASSERT_EQUAL_DOUBLE(1000.75, data.total);
    TEST_ASSERT_EQUAL_DOUBLE(25.0, data.daily);
    TEST_ASSERT_EQUAL_DOUBLE(150.25, data.weekly);
    TEST_ASSERT_EQUAL_DOUBLE(600.0, data.monthly);
    TEST_ASSERT_EQUAL_DOUBLE(7200.5, data.yearly);
    TEST_ASSERT_EQUAL_DOUBLE(3600.0, data.elapsed);
    TEST_ASSERT_EQUAL(42, data.switches);
    TEST_ASSERT_TRUE(data.imported);
    
    TEST_ASSERT_EQUAL(15, data.date.day);
    TEST_ASSERT_EQUAL(6, data.date.month);
    TEST_ASSERT_EQUAL(2023, data.date.year);
}

void test_energy_meter_data_reset_partial(void) {
    EnergyMeterData data;
    
    // Set some test values
    data.session = 50.0;
    data.total = 1000.0;
    data.daily = 25.0;
    data.weekly = 150.0;
    data.monthly = 600.0;
    data.yearly = 7200.0;
    data.elapsed = 3600.0;
    data.switches = 100;
    data.imported = true;
    
    // Partial reset (fullreset = false, import = false)
    data.reset(false, false);
    
    // Session data should be reset
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.session);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.elapsed);
    
    // Total and switches should be preserved
    TEST_ASSERT_EQUAL_DOUBLE(1000.0, data.total);
    TEST_ASSERT_EQUAL(100, data.switches);
    
    // Periodic counters should be reset
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.daily);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.weekly);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.monthly);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.yearly);
    
    // Import flag should be reset when import = false
    TEST_ASSERT_FALSE(data.imported);
}

void test_energy_meter_data_reset_full(void) {
    EnergyMeterData data;
    
    // Set some test values
    data.session = 50.0;
    data.total = 1000.0;
    data.switches = 100;
    data.imported = true;
    
    // Full reset (fullreset = true, import = false)
    data.reset(true, false);
    
    // Everything should be reset to zero
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.session);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.total);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, data.elapsed);
    TEST_ASSERT_EQUAL(0, data.switches);
    TEST_ASSERT_FALSE(data.imported);
}

void test_energy_meter_data_reset_with_import(void) {
    EnergyMeterData data;
    
    // Set imported to false initially
    data.imported = false;
    
    // Reset with import = true
    data.reset(false, true);
    
    // Import flag should be set to true
    TEST_ASSERT_TRUE(data.imported);
}

void test_energy_meter_data_serialize(void) {
    EnergyMeterData data;
    
    // Set test values
    data.session = 15.5;
    data.total = 1000.75;
    data.daily = 25.0;
    data.weekly = 150.25;
    data.monthly = 600.0;
    data.yearly = 7200.5;
    data.elapsed = 3600.0;
    data.switches = 42;
    data.imported = true;
    
    data.date.day = 15;
    data.date.month = 6;
    data.date.year = 2023;
    
    // Serialize to JSON
    StaticJsonDocument<capacity> doc;
    data.serialize(doc);
    
    // Check serialized values
    TEST_ASSERT_EQUAL_DOUBLE(15.5, doc["session"]);
    TEST_ASSERT_EQUAL_DOUBLE(1000.75, doc["total"]);
    TEST_ASSERT_EQUAL_DOUBLE(25.0, doc["daily"]);
    TEST_ASSERT_EQUAL_DOUBLE(150.25, doc["weekly"]);
    TEST_ASSERT_EQUAL_DOUBLE(600.0, doc["monthly"]);
    TEST_ASSERT_EQUAL_DOUBLE(7200.5, doc["yearly"]);
    TEST_ASSERT_EQUAL_DOUBLE(3600.0, doc["elapsed"]);
    TEST_ASSERT_EQUAL(42, doc["switches"]);
    TEST_ASSERT_TRUE(doc["imported"]);
    
    // Check date object
    JsonObject dateObj = doc["date"];
    TEST_ASSERT_EQUAL(15, dateObj["day"]);
    TEST_ASSERT_EQUAL(6, dateObj["month"]);
    TEST_ASSERT_EQUAL(2023, dateObj["year"]);
}

void test_energy_meter_data_deserialize(void) {
    EnergyMeterData data;
    
    // Create JSON document
    StaticJsonDocument<capacity> doc;
    doc["session"] = 25.5;
    doc["total"] = 2000.5;
    doc["daily"] = 50.0;
    doc["weekly"] = 300.0;
    doc["monthly"] = 1200.0;
    doc["yearly"] = 14400.0;
    doc["elapsed"] = 7200.0;
    doc["switches"] = 84;
    doc["imported"] = false;
    
    JsonObject dateObj = doc.createNestedObject("date");
    dateObj["day"] = 20;
    dateObj["month"] = 12;
    dateObj["year"] = 2023;
    
    // Deserialize
    data.deserialize(doc);
    
    // Check deserialized values
    TEST_ASSERT_EQUAL_DOUBLE(25.5, data.session);
    TEST_ASSERT_EQUAL_DOUBLE(2000.5, data.total);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, data.daily);
    TEST_ASSERT_EQUAL_DOUBLE(300.0, data.weekly);
    TEST_ASSERT_EQUAL_DOUBLE(1200.0, data.monthly);
    TEST_ASSERT_EQUAL_DOUBLE(14400.0, data.yearly);
    TEST_ASSERT_EQUAL_DOUBLE(7200.0, data.elapsed);
    TEST_ASSERT_EQUAL(84, data.switches);
    TEST_ASSERT_FALSE(data.imported);
    
    TEST_ASSERT_EQUAL(20, data.date.day);
    TEST_ASSERT_EQUAL(12, data.date.month);
    TEST_ASSERT_EQUAL(2023, data.date.year);
}

void test_energy_meter_data_deserialize_missing_fields(void) {
    EnergyMeterData data;
    
    // Set initial values
    data.session = 100.0;
    data.total = 5000.0;
    data.switches = 200;
    
    // Create JSON with only some fields
    StaticJsonDocument<capacity> doc;
    doc["session"] = 50.0;
    // Missing other fields
    
    // Deserialize should handle missing fields gracefully
    data.deserialize(doc);
    
    // Present field should be updated
    TEST_ASSERT_EQUAL_DOUBLE(50.0, data.session);
    
    // Missing fields should retain original values or be set to defaults
    // (depending on implementation)
}

void test_energy_meter_data_roundtrip_serialization(void) {
    EnergyMeterData data1, data2;
    
    // Set values in first object
    data1.session = 33.33;
    data1.total = 4444.44;
    data1.daily = 55.55;
    data1.weekly = 666.66;
    data1.monthly = 7777.77;
    data1.yearly = 88888.88;
    data1.elapsed = 9999.99;
    data1.switches = 123;
    data1.imported = true;
    
    data1.date.day = 31;
    data1.date.month = 12;
    data1.date.year = 2023;
    
    // Serialize
    StaticJsonDocument<capacity> doc;
    data1.serialize(doc);
    
    // Deserialize into second object
    data2.deserialize(doc);
    
    // Check values match (within floating point precision)
    TEST_ASSERT_EQUAL_DOUBLE(data1.session, data2.session);
    TEST_ASSERT_EQUAL_DOUBLE(data1.total, data2.total);
    TEST_ASSERT_EQUAL_DOUBLE(data1.daily, data2.daily);
    TEST_ASSERT_EQUAL_DOUBLE(data1.weekly, data2.weekly);
    TEST_ASSERT_EQUAL_DOUBLE(data1.monthly, data2.monthly);
    TEST_ASSERT_EQUAL_DOUBLE(data1.yearly, data2.yearly);
    TEST_ASSERT_EQUAL_DOUBLE(data1.elapsed, data2.elapsed);
    TEST_ASSERT_EQUAL(data1.switches, data2.switches);
    TEST_ASSERT_EQUAL(data1.imported, data2.imported);
    
    TEST_ASSERT_EQUAL(data1.date.day, data2.date.day);
    TEST_ASSERT_EQUAL(data1.date.month, data2.date.month);
    TEST_ASSERT_EQUAL(data1.date.year, data2.date.year);
}

void test_energy_meter_data_boundary_values(void) {
    EnergyMeterData data;
    
    // Test with boundary values
    data.session = 0.0;
    data.total = 999999.999;
    data.elapsed = 0.001;
    data.switches = 0;
    data.imported = false;
    
    data.date.day = 1;
    data.date.month = 1;
    data.date.year = 1900;
    
    // Serialize and deserialize
    StaticJsonDocument<capacity> doc;
    data.serialize(doc);
    
    EnergyMeterData data2;
    data2.deserialize(doc);
    
    // Values should be preserved
    TEST_ASSERT_EQUAL_DOUBLE(data.session, data2.session);
    TEST_ASSERT_EQUAL_DOUBLE(data.total, data2.total);
    TEST_ASSERT_EQUAL_DOUBLE(data.elapsed, data2.elapsed);
    TEST_ASSERT_EQUAL(data.switches, data2.switches);
    TEST_ASSERT_EQUAL(data.imported, data2.imported);
    
    TEST_ASSERT_EQUAL(data.date.day, data2.date.day);
    TEST_ASSERT_EQUAL(data.date.month, data2.date.month);
    TEST_ASSERT_EQUAL(data.date.year, data2.date.year);
}

void run_energy_meter_tests(void) {
    // EnergyMeterDate tests
    RUN_TEST(test_energy_meter_date_struct);
    RUN_TEST(test_energy_meter_date_copy);
    
    // EnergyMeterData tests
    RUN_TEST(test_energy_meter_data_constructor);
    RUN_TEST(test_energy_meter_data_assignment);
    RUN_TEST(test_energy_meter_data_reset_partial);
    RUN_TEST(test_energy_meter_data_reset_full);
    RUN_TEST(test_energy_meter_data_reset_with_import);
    RUN_TEST(test_energy_meter_data_serialize);
    RUN_TEST(test_energy_meter_data_deserialize);
    RUN_TEST(test_energy_meter_data_deserialize_missing_fields);
    RUN_TEST(test_energy_meter_data_roundtrip_serialization);
    RUN_TEST(test_energy_meter_data_boundary_values);
}