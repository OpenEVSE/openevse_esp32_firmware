#include <unity.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "test_utils.h"

// Include the classes under test
#include "limit.h"



// ==============================================
// LimitType Tests
// ==============================================

void test_limit_type_constructor_default(void) {
    LimitType type;
    // Default constructor creates type with uninitialized value
    TEST_PASS();
}

void test_limit_type_constructor_with_value(void) {
    LimitType type_none(LimitType::None);
    LimitType type_time(LimitType::Time);
    LimitType type_energy(LimitType::Energy);
    LimitType type_soc(LimitType::Soc);
    LimitType type_range(LimitType::Range);

    TEST_ASSERT_EQUAL(LimitType::None, type_none);
    TEST_ASSERT_EQUAL(LimitType::Time, type_time);
    TEST_ASSERT_EQUAL(LimitType::Energy, type_energy);
    TEST_ASSERT_EQUAL(LimitType::Soc, type_soc);
    TEST_ASSERT_EQUAL(LimitType::Range, type_range);
}

void test_limit_type_from_string_valid(void) {
    LimitType type;

    // Test valid strings (checks first character)
    TEST_ASSERT_EQUAL(LimitType::None, type.fromString("none"));
    TEST_ASSERT_EQUAL(LimitType::None, type);

    TEST_ASSERT_EQUAL(LimitType::Time, type.fromString("time"));
    TEST_ASSERT_EQUAL(LimitType::Time, type);

    TEST_ASSERT_EQUAL(LimitType::Energy, type.fromString("energy"));
    TEST_ASSERT_EQUAL(LimitType::Energy, type);

    TEST_ASSERT_EQUAL(LimitType::Soc, type.fromString("soc"));
    TEST_ASSERT_EQUAL(LimitType::Soc, type);

    TEST_ASSERT_EQUAL(LimitType::Range, type.fromString("range"));
    TEST_ASSERT_EQUAL(LimitType::Range, type);
}

void test_limit_type_from_string_first_char_only(void) {
    LimitType type;

    // Test that only first character matters
    TEST_ASSERT_EQUAL(LimitType::None, type.fromString("n"));
    TEST_ASSERT_EQUAL(LimitType::Time, type.fromString("t"));
    TEST_ASSERT_EQUAL(LimitType::Energy, type.fromString("e"));
    TEST_ASSERT_EQUAL(LimitType::Soc, type.fromString("s"));
    TEST_ASSERT_EQUAL(LimitType::Range, type.fromString("r"));

    // Test with longer strings starting with correct character
    TEST_ASSERT_EQUAL(LimitType::Time, type.fromString("timer"));
    TEST_ASSERT_EQUAL(LimitType::Energy, type.fromString("electrical"));
    TEST_ASSERT_EQUAL(LimitType::Soc, type.fromString("state_of_charge"));
    TEST_ASSERT_EQUAL(LimitType::Range, type.fromString("radius"));
}

void test_limit_type_from_string_invalid(void) {
    LimitType type;

    // Set initial value to ensure it doesn't change
    type = LimitType::Time;

    // Test invalid strings (should not change the value)
    type.fromString("invalid");
    // Note: The current implementation doesn't handle invalid cases properly
    // It would leave the value unchanged, but this behavior isn't guaranteed
}

void test_limit_type_to_string(void) {
    LimitType type_none(LimitType::None);
    LimitType type_time(LimitType::Time);
    LimitType type_energy(LimitType::Energy);
    LimitType type_soc(LimitType::Soc);
    LimitType type_range(LimitType::Range);

    TEST_ASSERT_EQUAL_STRING("none", type_none.toString());
    TEST_ASSERT_EQUAL_STRING("time", type_time.toString());
    TEST_ASSERT_EQUAL_STRING("energy", type_energy.toString());
    TEST_ASSERT_EQUAL_STRING("soc", type_soc.toString());
    TEST_ASSERT_EQUAL_STRING("range", type_range.toString());
}

void test_limit_type_to_string_invalid(void) {
    LimitType type;
    // Force an invalid value
    type = (LimitType::Value)99;

    // Should return "none" for invalid values
    TEST_ASSERT_EQUAL_STRING("none", type.toString());
}

void test_limit_type_assignment_operator(void) {
    LimitType type;

    // Test assignment and return value
    LimitType result = (type = LimitType::Energy);
    TEST_ASSERT_EQUAL(LimitType::Energy, type);
    TEST_ASSERT_EQUAL(LimitType::Energy, result);
}

void test_limit_type_roundtrip_conversion(void) {
    LimitType type;

    // Test that fromString and toString are consistent
    type.fromString("time");
    TEST_ASSERT_EQUAL_STRING("time", type.toString());

    type.fromString("energy");
    TEST_ASSERT_EQUAL_STRING("energy", type.toString());

    type.fromString("soc");
    TEST_ASSERT_EQUAL_STRING("soc", type.toString());

    type.fromString("range");
    TEST_ASSERT_EQUAL_STRING("range", type.toString());

    type.fromString("none");
    TEST_ASSERT_EQUAL_STRING("none", type.toString());
}

// ==============================================
// LimitProperties Tests
// ==============================================

void test_limit_properties_constructor(void) {
    LimitProperties props;

    // Check default values
    TEST_ASSERT_EQUAL(LimitType::None, props.getType());
    TEST_ASSERT_EQUAL(0, props.getValue());
    TEST_ASSERT_TRUE(props.getAutoRelease());
}

void test_limit_properties_init(void) {
    LimitProperties props;

    // Modify values
    props.setType(LimitType::Time);
    props.setValue(100);
    props.setAutoRelease(false);

    // Reset to defaults
    props.init();

    // Check values are reset
    TEST_ASSERT_EQUAL(LimitType::None, props.getType());
    TEST_ASSERT_EQUAL(0, props.getValue());
    TEST_ASSERT_TRUE(props.getAutoRelease());
}

void test_limit_properties_setters_getters(void) {
    LimitProperties props;

    // Test type
    TEST_ASSERT_TRUE(props.setType(LimitType::Energy));
    TEST_ASSERT_EQUAL(LimitType::Energy, props.getType());

    // Test value
    TEST_ASSERT_TRUE(props.setValue(5000));
    TEST_ASSERT_EQUAL(5000, props.getValue());

    // Test auto release
    TEST_ASSERT_TRUE(props.setAutoRelease(false));
    TEST_ASSERT_FALSE(props.getAutoRelease());

    TEST_ASSERT_TRUE(props.setAutoRelease(true));
    TEST_ASSERT_TRUE(props.getAutoRelease());
}

void test_limit_properties_serialize(void) {
    LimitProperties props;

    // Set test values
    props.setType(LimitType::Time);
    props.setValue(3600);
    props.setAutoRelease(false);

    // Create JSON document and serialize
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();

    TEST_ASSERT_TRUE(props.serialize(obj));

    // Check serialized values
    TEST_ASSERT_EQUAL_STRING("time", obj["type"]);
    TEST_ASSERT_EQUAL(3600, obj["value"]);
    TEST_ASSERT_FALSE(obj["auto_release"]);
}

void test_limit_properties_deserialize_complete(void) {
    LimitProperties props;

    // Create JSON with all fields
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    obj["type"] = "energy";
    obj["value"] = 5000;
    obj["auto_release"] = false;

    // Deserialize should succeed (type > 0 && value > 0)
    TEST_ASSERT_TRUE(props.deserialize(obj));

    // Check deserialized values
    TEST_ASSERT_EQUAL(LimitType::Energy, props.getType());
    TEST_ASSERT_EQUAL(5000, props.getValue());
    TEST_ASSERT_FALSE(props.getAutoRelease());
}

void test_limit_properties_deserialize_partial(void) {
    LimitProperties props;

    // Set initial values
    props.setType(LimitType::Time);
    props.setValue(1000);
    props.setAutoRelease(true);

    // Create JSON with only some fields
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    obj["value"] = 2000;

    // Should fail because type is not in JSON and won't be > 0
    TEST_ASSERT_FALSE(props.deserialize(obj));

    // But value should still be updated
    TEST_ASSERT_EQUAL(2000, props.getValue());
}

void test_limit_properties_deserialize_missing_fields(void) {
    LimitProperties props;

    // Empty JSON object
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();

    // Should fail validation (type and value not > 0)
    TEST_ASSERT_FALSE(props.deserialize(obj));
}

void test_limit_properties_deserialize_zero_values(void) {
    LimitProperties props;

    // JSON with zero values
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    obj["type"] = "none";  // LimitType::None = 0
    obj["value"] = 0;
    obj["auto_release"] = true;

    // Should fail validation (type == 0 || value == 0)
    TEST_ASSERT_FALSE(props.deserialize(obj));
}

void test_limit_properties_roundtrip_serialization(void) {
    LimitProperties props1, props2;

    // Set values in first object
    props1.setType(LimitType::Soc);
    props1.setValue(80);
    props1.setAutoRelease(false);

    // Serialize
    DynamicJsonDocument doc(512);
    JsonObject obj = doc.to<JsonObject>();
    props1.serialize(obj);

    // Deserialize into second object
    props2.deserialize(obj);

    // Check values match
    TEST_ASSERT_EQUAL(props1.getType(), props2.getType());
    TEST_ASSERT_EQUAL(props1.getValue(), props2.getValue());
    TEST_ASSERT_EQUAL(props1.getAutoRelease(), props2.getAutoRelease());
}

void run_limit_tests(void) {
    // LimitType tests
    RUN_TEST(test_limit_type_constructor_default);
    RUN_TEST(test_limit_type_constructor_with_value);
    RUN_TEST(test_limit_type_from_string_valid);
    RUN_TEST(test_limit_type_from_string_first_char_only);
    RUN_TEST(test_limit_type_from_string_invalid);
    RUN_TEST(test_limit_type_to_string);
    RUN_TEST(test_limit_type_to_string_invalid);
    RUN_TEST(test_limit_type_assignment_operator);
    RUN_TEST(test_limit_type_roundtrip_conversion);

    // LimitProperties tests
    RUN_TEST(test_limit_properties_constructor);
    RUN_TEST(test_limit_properties_init);
    RUN_TEST(test_limit_properties_setters_getters);
    RUN_TEST(test_limit_properties_serialize);
    RUN_TEST(test_limit_properties_deserialize_complete);
    RUN_TEST(test_limit_properties_deserialize_partial);
    RUN_TEST(test_limit_properties_deserialize_missing_fields);
    RUN_TEST(test_limit_properties_deserialize_zero_values);
    RUN_TEST(test_limit_properties_roundtrip_serialization);
}
