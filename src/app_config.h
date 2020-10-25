#ifndef _EMONESP_CONFIG_H
#define _EMONESP_CONFIG_H

#include <Arduino.h>
#include <ArduinoJson.h>

// -------------------------------------------------------------------
// Load and save the OpenEVSE WiFi config.
//
// This initial implementation saves the config to the EEPROM area of flash
// -------------------------------------------------------------------

// Global config varables

// Wifi Network Strings
extern String esid;
extern String epass;
extern String apass;

// Web server authentication (leave blank for none)
extern String www_username;
extern String www_password;

// Advanced settings
extern String esp_hostname;
extern String esp_hostname_default;
extern String sntp_hostname;

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
extern String mqtt_solar;
extern String mqtt_grid_ie;
extern String mqtt_vrms;
extern String mqtt_announce_topic;

// Time
extern String time_zone;

// Divert settings
extern double divert_PV_ratio;
extern double divert_attack_smoothing_factor;
extern double divert_decay_smoothing_factor;
extern uint32_t divert_min_charge_time;

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
#define CONFIG_PAUSE_USES_DISABLED   (1 << 13)

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

inline bool config_mqtt_reject_unauthorized() {
  return 0 == (flags & CONFIG_MQTT_ALLOW_ANY_CERT);
}

inline bool config_tesla_enabled() {
  return CONFIG_SERVICE_TESLA == (flags & CONFIG_SERVICE_TESLA);
}

inline bool config_divert_enabled() {
  return CONFIG_SERVICE_DIVERT == (flags & CONFIG_SERVICE_DIVERT);
}

inline uint8_t config_charge_mode() {
  return (flags & CONFIG_CHARGE_MODE) >> 10;
}

inline bool config_pause_uses_disabled() {
  return CONFIG_PAUSE_USES_DISABLED == (flags & CONFIG_PAUSE_USES_DISABLED);
}

// Ohm Connect Settings
extern String ohm;

// -------------------------------------------------------------------
// Load saved settings
// -------------------------------------------------------------------
extern void config_load_settings();
extern void config_load_v1_settings();

// -------------------------------------------------------------------
// Save the EmonCMS server details
// -------------------------------------------------------------------
extern void config_save_emoncms(bool enable, String server, String node, String apikey, String fingerprint);

// -------------------------------------------------------------------
// Save the MQTT broker details
// -------------------------------------------------------------------
extern void config_save_mqtt(bool enable, int protocol, String server, uint16_t port, String topic, String user, String pass, String solar, String grid_ie, bool reject_unauthorized);

// -------------------------------------------------------------------
// Save the admin/web interface details
// -------------------------------------------------------------------
extern void config_save_admin(String user, String pass);

// -------------------------------------------------------------------
// Save the SNTP settings
// -------------------------------------------------------------------
extern void config_save_sntp(bool enable, String tz);

// -------------------------------------------------------------------
// Save advanced settings
// -------------------------------------------------------------------
extern void config_save_advanced(String hostname, String sntp_host);

// -------------------------------------------------------------------
// Save the Wifi details
// -------------------------------------------------------------------
extern void config_save_wifi(String qsid, String qpass);

// -------------------------------------------------------------------
// Save the Ohm settings
// -------------------------------------------------------------------
extern void config_save_ohm(bool enable, String qohm);

// -------------------------------------------------------------------
// Save the flags
// -------------------------------------------------------------------
extern void config_save_flags(uint32_t flags);

// -------------------------------------------------------------------
// Reset the config back to defaults
// -------------------------------------------------------------------
extern void config_reset();

void config_set_timezone(String tz);

void config_set(const char *name, uint32_t val);
void config_set(const char *name, String val);
void config_set(const char *name, bool val);
void config_set(const char *name, double val);

// Read config settings from JSON object
bool config_deserialize(String& json);
bool config_deserialize(const char *json);
bool config_deserialize(DynamicJsonDocument &doc);
void config_commit();

// Write config settings to JSON object
bool config_serialize(String& json, bool longNames = true, bool compactOutput = false, bool hideSecrets = false);
bool config_serialize(DynamicJsonDocument &doc, bool longNames = true, bool compactOutput = false, bool hideSecrets = false);

#endif // _EMONESP_CONFIG_H
