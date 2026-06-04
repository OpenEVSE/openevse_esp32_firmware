#ifndef _EMONESP_CONFIG_H
#define _EMONESP_CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "evse_state.h"

#ifndef ENABLE_CONFIG_V1_IMPORT
#define ENABLE_CONFIG_V1_IMPORT 1
#endif

#ifndef ENABLE_CONFIG_CHANGE_NOTIFICATION
#define ENABLE_CONFIG_CHANGE_NOTIFICATION 1
#endif

// -------------------------------------------------------------------
// Load and save the OpenEVSE WiFi config.
//
// This initial implementation saves the config to the EEPROM area of flash
// -------------------------------------------------------------------

// Global config varables

// Wifi Network Strings
extern String esid;
extern String epass;
extern String ap_ssid;
extern String ap_pass;
// Language
extern String lang;

// Web server authentication (leave blank for none)
extern String www_username;
extern String www_password;
extern String www_certificate_id;

// Advanced settings
extern String esp_hostname;
extern String esp_hostname_default;
extern String sntp_hostname;

// LIMIT Settings
extern String limit_default_type;
extern uint32_t limit_default_value;

// EMONCMS SERVER strings
extern String emoncms_server;
extern String emoncms_node;
extern String emoncms_apikey;
extern String emoncms_fingerprint;

// MQTT Settings
extern String mqtt_server;
extern uint32_t mqtt_port;
extern String mqtt_topic;
extern String mqtt_user;
extern String mqtt_pass;
extern String mqtt_certificate_id;
extern String mqtt_solar;
extern String mqtt_grid_ie;
extern String mqtt_vrms;
extern String mqtt_live_pwr;
extern String mqtt_vehicle_soc;
extern String mqtt_vehicle_range;
extern String mqtt_vehicle_eta;
extern String mqtt_announce_topic;

// OCPP 1.6 Settings
extern String ocpp_server;
extern String ocpp_chargeBoxId;
extern String ocpp_authkey;
extern String ocpp_idtag;

// RFID storage
extern String rfid_storage;

// Time
extern String time_zone;

// Divert settings
extern int8_t divert_type;
extern double divert_PV_ratio;
extern uint32_t divert_attack_smoothing_time;
extern uint32_t divert_decay_smoothing_time;
extern uint32_t divert_min_charge_time;

// Scheduler settings
extern uint32_t scheduler_start_window;

//Shaper settings
extern uint32_t current_shaper_max_pwr;
extern uint32_t current_shaper_smoothing_time;
extern uint32_t current_shaper_min_pause_time;
extern uint32_t current_shaper_data_maxinterval;

// Vehicle
extern uint8_t vehicle_data_src;

enum vehicle_data_src {
  VEHICLE_DATA_SRC_NONE,
  VEHICLE_DATA_SRC_TESLA,
  VEHICLE_DATA_SRC_MQTT,
  VEHICLE_DATA_SRC_HTTP
};

// 24-bits of Flags
extern uint32_t flags;

#define CONFIG_SERVICE_EMONCMS      (1 << 0)
#define CONFIG_SERVICE_MQTT         (1 << 1)
#define CONFIG_SERVICE_OHM          (1 << 2)
#define CONFIG_SERVICE_SNTP         (1 << 3)
#define CONFIG_MQTT_PROTOCOL        (7 << 4) // Maybe leave a bit of space after for additional protocols
#define CONFIG_MQTT_ALLOW_ANY_CERT  (1 << 7)
#define CONFIG_SERVICE_TESLA        (1 << 8)
#define CONFIG_SERVICE_DIVERT       (1 << 9)
#define CONFIG_CHARGE_MODE          (7 << 10) // 3 bits for mode
#define CONFIG_PAUSE_USES_DISABLED  (1 << 13)
#define CONFIG_SERVICE_OCPP         (1 << 14)
#define CONFIG_OCPP_ACCESS_SUSPEND  (1 << 15)
#define CONFIG_OCPP_ACCESS_ENERGIZE (1 << 16)
#define CONFIG_VEHICLE_RANGE_MILES  (1 << 17)
#define CONFIG_RFID                 (1 << 18)
#define CONFIG_SERVICE_CUR_SHAPER   (1 << 19)
#define CONFIG_MQTT_RETAINED        (1 << 20)
#define CONFIG_FACTORY_WRITE_LOCK   (1 << 21)
#define CONFIG_OCPP_AUTO_AUTH       (1 << 22)
#define CONFIG_OCPP_OFFLINE_AUTH    (1 << 23)
#define CONFIG_THREEPHASE           (1 << 24)
#define CONFIG_WIZARD               (1 << 25)
#define CONFIG_DEFAULT_STATE        (1 << 26)

#define INITIAL_CONFIG_VERSION  1

inline bool config_emoncms_enabled() {
  return CONFIG_SERVICE_EMONCMS == (flags & CONFIG_SERVICE_EMONCMS);
}

inline bool config_mqtt_enabled() {
  return CONFIG_SERVICE_MQTT == (flags & CONFIG_SERVICE_MQTT);
}

inline bool config_ohm_enabled() {
  return CONFIG_SERVICE_OHM == (flags & CONFIG_SERVICE_OHM);
}

inline bool config_sntp_enabled() {
  return CONFIG_SERVICE_SNTP == (flags & CONFIG_SERVICE_SNTP);
}

inline uint8_t config_mqtt_protocol() {
  return (flags & CONFIG_MQTT_PROTOCOL) >> 4;
}

inline bool config_mqtt_retained() {
  return CONFIG_MQTT_RETAINED == (flags & CONFIG_MQTT_RETAINED);
}

inline bool config_mqtt_reject_unauthorized() {
  return 0 == (flags & CONFIG_MQTT_ALLOW_ANY_CERT);
}

inline bool config_ocpp_enabled() {
  return CONFIG_SERVICE_OCPP == (flags & CONFIG_SERVICE_OCPP);
}

inline bool config_ocpp_access_can_suspend() {
  return CONFIG_OCPP_ACCESS_SUSPEND == (flags & CONFIG_OCPP_ACCESS_SUSPEND);
}

inline bool config_ocpp_access_can_energize() {
  return CONFIG_OCPP_ACCESS_ENERGIZE == (flags & CONFIG_OCPP_ACCESS_ENERGIZE);
}

inline bool config_ocpp_auto_authorization() {
  return CONFIG_OCPP_AUTO_AUTH == (flags & CONFIG_OCPP_AUTO_AUTH);
}

inline bool config_ocpp_offline_authorization() {
  return CONFIG_OCPP_OFFLINE_AUTH == (flags & CONFIG_OCPP_OFFLINE_AUTH);
}

inline bool config_divert_enabled() {
  return CONFIG_SERVICE_DIVERT == (flags & CONFIG_SERVICE_DIVERT);
}

inline bool config_current_shaper_enabled() {
  return CONFIG_SERVICE_CUR_SHAPER == (flags & CONFIG_SERVICE_CUR_SHAPER);
}

inline uint8_t config_charge_mode() {
  return (flags & CONFIG_CHARGE_MODE) >> 10;
}

inline bool config_pause_uses_disabled() {
  return CONFIG_PAUSE_USES_DISABLED == (flags & CONFIG_PAUSE_USES_DISABLED);
}

inline bool config_vehicle_range_miles() {
  return CONFIG_VEHICLE_RANGE_MILES == (flags & CONFIG_VEHICLE_RANGE_MILES);
}

inline bool config_rfid_enabled() {
  return CONFIG_RFID == (flags & CONFIG_RFID);
}

inline bool config_factory_write_lock() {
  return CONFIG_FACTORY_WRITE_LOCK == (flags & CONFIG_FACTORY_WRITE_LOCK);
}

inline bool config_threephase_enabled() {
  return CONFIG_THREEPHASE == (flags & CONFIG_THREEPHASE);
}

inline bool config_wizard_passed()
{
  return CONFIG_WIZARD == (flags & CONFIG_WIZARD);
}

inline EvseState config_default_state()
{
  return CONFIG_DEFAULT_STATE == (flags & CONFIG_DEFAULT_STATE) ? EvseState::Active : EvseState::Disabled;
}

// Ohm Connect Settings
extern String ohm;

extern uint32_t config_version();

// -------------------------------------------------------------------
// Load saved settings
// -------------------------------------------------------------------
extern void config_load_settings();
#if ENABLE_CONFIG_V1_IMPORT
extern void config_load_v1_settings();
#endif

// -------------------------------------------------------------------
// Reset the config back to defaults
// -------------------------------------------------------------------
extern void config_reset();

void config_set(const char *name, uint32_t val);
void config_set(const char *name, String val);
void config_set(const char *name, bool val);
void config_set(const char *name, double val);

// Read config settings from JSON object
bool config_deserialize(String& json);
bool config_deserialize(const char *json);
bool config_deserialize(DynamicJsonDocument &doc);
void config_commit(bool factory = false);

// Write config settings to JSON object
bool config_serialize(String& json, bool longNames = true, bool compactOutput = false, bool hideSecrets = false);
bool config_serialize(DynamicJsonDocument &doc, bool longNames = true, bool compactOutput = false, bool hideSecrets = false);

#endif // _EMONESP_CONFIG_H
