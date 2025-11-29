#include <unity.h>
#include <Arduino.h>
#include "test_utils.h"

// Include the config system under test
#include "app_config.h"



// ==============================================
// Configuration Flag Tests
// ==============================================

void test_config_emoncms_enabled(void) {
    // Test disabled
    flags = 0;
    TEST_ASSERT_FALSE(config_emoncms_enabled());
    
    // Test enabled
    flags = CONFIG_SERVICE_EMONCMS;
    TEST_ASSERT_TRUE(config_emoncms_enabled());
    
    // Test with other flags set
    flags = CONFIG_SERVICE_MQTT | CONFIG_SERVICE_EMONCMS;
    TEST_ASSERT_TRUE(config_emoncms_enabled());
    
    // Test with only other flags
    flags = CONFIG_SERVICE_MQTT;
    TEST_ASSERT_FALSE(config_emoncms_enabled());
}

void test_config_mqtt_enabled(void) {
    // Test disabled
    flags = 0;
    TEST_ASSERT_FALSE(config_mqtt_enabled());
    
    // Test enabled
    flags = CONFIG_SERVICE_MQTT;
    TEST_ASSERT_TRUE(config_mqtt_enabled());
    
    // Test with other flags
    flags = CONFIG_SERVICE_EMONCMS | CONFIG_SERVICE_MQTT;
    TEST_ASSERT_TRUE(config_mqtt_enabled());
}

void test_config_ohm_enabled(void) {
    // Test disabled
    flags = 0;
    TEST_ASSERT_FALSE(config_ohm_enabled());
    
    // Test enabled
    flags = CONFIG_SERVICE_OHM;
    TEST_ASSERT_TRUE(config_ohm_enabled());
}

void test_config_sntp_enabled(void) {
    // Test disabled
    flags = 0;
    TEST_ASSERT_FALSE(config_sntp_enabled());
    
    // Test enabled
    flags = CONFIG_SERVICE_SNTP;
    TEST_ASSERT_TRUE(config_sntp_enabled());
}

void test_config_mqtt_protocol(void) {
    // Test default protocol (0)
    flags = 0;
    TEST_ASSERT_EQUAL(0, config_mqtt_protocol());
    
    // Test protocol 1
    flags = (1 << 4);
    TEST_ASSERT_EQUAL(1, config_mqtt_protocol());
    
    // Test protocol 7 (max 3-bit value)
    flags = (7 << 4);
    TEST_ASSERT_EQUAL(7, config_mqtt_protocol());
    
    // Test with other flags set
    flags = CONFIG_SERVICE_MQTT | (3 << 4);
    TEST_ASSERT_EQUAL(3, config_mqtt_protocol());
}

void test_config_mqtt_retained(void) {
    // Test disabled
    flags = 0;
    TEST_ASSERT_FALSE(config_mqtt_retained());
    
    // Test enabled
    flags = CONFIG_MQTT_RETAINED;
    TEST_ASSERT_TRUE(config_mqtt_retained());
}

void test_config_mqtt_reject_unauthorized(void) {
    // Test default (should reject unauthorized)
    flags = 0;
    TEST_ASSERT_TRUE(config_mqtt_reject_unauthorized());
    
    // Test allow any cert
    flags = CONFIG_MQTT_ALLOW_ANY_CERT;
    TEST_ASSERT_FALSE(config_mqtt_reject_unauthorized());
}

void test_config_ocpp_enabled(void) {
    // Test disabled
    flags = 0;
    TEST_ASSERT_FALSE(config_ocpp_enabled());
    
    // Test enabled
    flags = CONFIG_SERVICE_OCPP;
    TEST_ASSERT_TRUE(config_ocpp_enabled());
}

void test_config_ocpp_access_functions(void) {
    // Test suspend capability
    flags = 0;
    TEST_ASSERT_FALSE(config_ocpp_access_can_suspend());
    
    flags = CONFIG_OCPP_ACCESS_SUSPEND;
    TEST_ASSERT_TRUE(config_ocpp_access_can_suspend());
    
    // Test energize capability
    flags = 0;
    TEST_ASSERT_FALSE(config_ocpp_access_can_energize());
    
    flags = CONFIG_OCPP_ACCESS_ENERGIZE;
    TEST_ASSERT_TRUE(config_ocpp_access_can_energize());
}

void test_config_ocpp_authorization_functions(void) {
    // Test auto authorization
    flags = 0;
    TEST_ASSERT_FALSE(config_ocpp_auto_authorization());
    
    flags = CONFIG_OCPP_AUTO_AUTH;
    TEST_ASSERT_TRUE(config_ocpp_auto_authorization());
    
    // Test offline authorization
    flags = 0;
    TEST_ASSERT_FALSE(config_ocpp_offline_authorization());
    
    flags = CONFIG_OCPP_OFFLINE_AUTH;
    TEST_ASSERT_TRUE(config_ocpp_offline_authorization());
}

void test_config_divert_enabled(void) {
    // Test disabled
    flags = 0;
    TEST_ASSERT_FALSE(config_divert_enabled());
    
    // Test enabled
    flags = CONFIG_SERVICE_DIVERT;
    TEST_ASSERT_TRUE(config_divert_enabled());
}

void test_config_current_shaper_enabled(void) {
    // Test disabled
    flags = 0;
    TEST_ASSERT_FALSE(config_current_shaper_enabled());
    
    // Test enabled
    flags = CONFIG_SERVICE_CUR_SHAPER;
    TEST_ASSERT_TRUE(config_current_shaper_enabled());
}

void test_config_charge_mode(void) {
    // Test default mode (0)
    flags = 0;
    TEST_ASSERT_EQUAL(0, config_charge_mode());
    
    // Test mode 1
    flags = (1 << 10);
    TEST_ASSERT_EQUAL(1, config_charge_mode());
    
    // Test mode 7 (max 3-bit value)
    flags = (7 << 10);
    TEST_ASSERT_EQUAL(7, config_charge_mode());
    
    // Test with other flags set
    flags = CONFIG_SERVICE_MQTT | (5 << 10);
    TEST_ASSERT_EQUAL(5, config_charge_mode());
}

void test_config_pause_uses_disabled(void) {
    // Test default
    flags = 0;
    TEST_ASSERT_FALSE(config_pause_uses_disabled());
    
    // Test enabled
    flags = CONFIG_PAUSE_USES_DISABLED;
    TEST_ASSERT_TRUE(config_pause_uses_disabled());
}

void test_config_vehicle_range_miles(void) {
    // Test default (should be false - km)
    flags = 0;
    TEST_ASSERT_FALSE(config_vehicle_range_miles());
    
    // Test miles enabled
    flags = CONFIG_VEHICLE_RANGE_MILES;
    TEST_ASSERT_TRUE(config_vehicle_range_miles());
}

void test_config_rfid_enabled(void) {
    // Test disabled
    flags = 0;
    TEST_ASSERT_FALSE(config_rfid_enabled());
    
    // Test enabled
    flags = CONFIG_RFID;
    TEST_ASSERT_TRUE(config_rfid_enabled());
}

void test_config_factory_write_lock(void) {
    // Test unlocked
    flags = 0;
    TEST_ASSERT_FALSE(config_factory_write_lock());
    
    // Test locked
    flags = CONFIG_FACTORY_WRITE_LOCK;
    TEST_ASSERT_TRUE(config_factory_write_lock());
}

void test_config_threephase_enabled(void) {
    // Test disabled
    flags = 0;
    TEST_ASSERT_FALSE(config_threephase_enabled());
    
    // Test enabled
    flags = CONFIG_THREEPHASE;
    TEST_ASSERT_TRUE(config_threephase_enabled());
}

void test_config_wizard_passed(void) {
    // Test not passed
    flags = 0;
    TEST_ASSERT_FALSE(config_wizard_passed());
    
    // Test passed
    flags = CONFIG_WIZARD;
    TEST_ASSERT_TRUE(config_wizard_passed());
}

void test_config_default_state(void) {
    // Test default disabled state
    flags = 0;
    EvseState state = config_default_state();
    TEST_ASSERT_EQUAL(EvseState::Disabled, state);
    
    // Test active state
    flags = CONFIG_DEFAULT_STATE;
    state = config_default_state();
    TEST_ASSERT_EQUAL(EvseState::Active, state);
}

void test_multiple_flags_interaction(void) {
    // Test multiple flags can be set simultaneously
    flags = CONFIG_SERVICE_MQTT | CONFIG_SERVICE_EMONCMS | CONFIG_SERVICE_DIVERT;
    
    TEST_ASSERT_TRUE(config_mqtt_enabled());
    TEST_ASSERT_TRUE(config_emoncms_enabled());
    TEST_ASSERT_TRUE(config_divert_enabled());
    TEST_ASSERT_FALSE(config_ocpp_enabled());
}

void test_flag_bit_positions(void) {
    // Test that flag values are powers of 2 (single bit set)
    uint32_t flag_values[] = {
        CONFIG_SERVICE_EMONCMS,
        CONFIG_SERVICE_MQTT,
        CONFIG_SERVICE_OHM,
        CONFIG_SERVICE_SNTP,
        CONFIG_MQTT_ALLOW_ANY_CERT,
        CONFIG_SERVICE_TESLA,
        CONFIG_SERVICE_DIVERT,
        CONFIG_PAUSE_USES_DISABLED,
        CONFIG_SERVICE_OCPP,
        CONFIG_OCPP_ACCESS_SUSPEND,
        CONFIG_OCPP_ACCESS_ENERGIZE,
        CONFIG_VEHICLE_RANGE_MILES,
        CONFIG_RFID,
        CONFIG_SERVICE_CUR_SHAPER,
        CONFIG_MQTT_RETAINED,
        CONFIG_FACTORY_WRITE_LOCK,
        CONFIG_OCPP_AUTO_AUTH,
        CONFIG_OCPP_OFFLINE_AUTH,
        CONFIG_THREEPHASE,
        CONFIG_WIZARD,
        CONFIG_DEFAULT_STATE
    };
    
    size_t num_flags = sizeof(flag_values) / sizeof(flag_values[0]);
    
    for (size_t i = 0; i < num_flags; i++) {
        uint32_t flag = flag_values[i];
        
        // Check that only one bit is set (power of 2)
        TEST_ASSERT_TRUE((flag & (flag - 1)) == 0);
        
        // Check that flag is not zero
        TEST_ASSERT_NOT_EQUAL(0, flag);
    }
}

void test_multi_bit_fields(void) {
    // Test CONFIG_MQTT_PROTOCOL field (3 bits)
    uint32_t protocol_mask = CONFIG_MQTT_PROTOCOL;
    TEST_ASSERT_EQUAL(7 << 4, protocol_mask);  // Should be 3 bits at position 4
    
    // Test CONFIG_CHARGE_MODE field (3 bits)
    uint32_t charge_mask = CONFIG_CHARGE_MODE;
    TEST_ASSERT_EQUAL(7 << 10, charge_mask);  // Should be 3 bits at position 10
}

void test_flag_isolation(void) {
    // Test that setting one flag doesn't affect others
    flags = 0;
    
    flags |= CONFIG_SERVICE_MQTT;
    TEST_ASSERT_TRUE(config_mqtt_enabled());
    TEST_ASSERT_FALSE(config_emoncms_enabled());
    TEST_ASSERT_FALSE(config_divert_enabled());
    
    flags |= CONFIG_SERVICE_DIVERT;
    TEST_ASSERT_TRUE(config_mqtt_enabled());
    TEST_ASSERT_TRUE(config_divert_enabled());
    TEST_ASSERT_FALSE(config_emoncms_enabled());
}

void run_app_config_tests(void) {
    RUN_TEST(test_config_emoncms_enabled);
    RUN_TEST(test_config_mqtt_enabled);
    RUN_TEST(test_config_ohm_enabled);
    RUN_TEST(test_config_sntp_enabled);
    RUN_TEST(test_config_mqtt_protocol);
    RUN_TEST(test_config_mqtt_retained);
    RUN_TEST(test_config_mqtt_reject_unauthorized);
    RUN_TEST(test_config_ocpp_enabled);
    RUN_TEST(test_config_ocpp_access_functions);
    RUN_TEST(test_config_ocpp_authorization_functions);
    RUN_TEST(test_config_divert_enabled);
    RUN_TEST(test_config_current_shaper_enabled);
    RUN_TEST(test_config_charge_mode);
    RUN_TEST(test_config_pause_uses_disabled);
    RUN_TEST(test_config_vehicle_range_miles);
    RUN_TEST(test_config_rfid_enabled);
    RUN_TEST(test_config_factory_write_lock);
    RUN_TEST(test_config_threephase_enabled);
    RUN_TEST(test_config_wizard_passed);
    RUN_TEST(test_config_default_state);
    RUN_TEST(test_multiple_flags_interaction);
    RUN_TEST(test_flag_bit_positions);
    RUN_TEST(test_multi_bit_fields);
    RUN_TEST(test_flag_isolation);
}