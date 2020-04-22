#include "emonesp.h"
#include "app_config.h"
#include "hal.h"

#include <Arduino.h>
#include <EEPROM.h>             // Save config settings
#include <ArduinoJson.h>

bool modified = false;

#define DEF_VALUE(type, val, d) \
type val = d;\
void config_set_ ## val (type value) \
{ \
  if(val != value) { \
    val = value; \
    modified = true; \
  } \
}

// Wifi Network Strings
DEF_VALUE(String, esid, "");
DEF_VALUE(String, epass, "");

// Web server authentication (leave blank for none)
DEF_VALUE(String, www_username, "");
DEF_VALUE(String, www_password, "");

// Advanced setting)s
DEF_VALUE(String, esp_hostname, "");
DEF_VALUE(String, esp_hostname_default, "openevse-"+HAL.getShortId());
DEF_VALUE(String, sntp_hostname, "");

// Time
DEF_VALUE(String, time_zone, "";);

// EMONCMS SERVER strings
DEF_VALUE(String, emoncms_server, "");
DEF_VALUE(String, emoncms_node, "");
DEF_VALUE(String, emoncms_apikey, "");
DEF_VALUE(String, emoncms_fingerprint, "");

// MQTT Settings
DEF_VALUE(String, mqtt_server, "");
DEF_VALUE(uint32_t, mqtt_port, 1883);
DEF_VALUE(String, mqtt_topic, "");
DEF_VALUE(String, mqtt_user, "");
DEF_VALUE(String, mqtt_pass, "");
DEF_VALUE(String, mqtt_solar, "");
DEF_VALUE(String, mqtt_grid_ie, "");
DEF_VALUE(String, mqtt_announce_topic, "openevse/announce/"+HAL.getShortId());

// Ohm Connect Settings
DEF_VALUE(String, ohm, "");

// Flags
DEF_VALUE(uint32_t, flags, 0);

#define esp_hostname_LONG_NAME          "esp_hostname"
#define esid_LONG_NAME                  "ssid"
#define epass_LONG_NAME                 "pass"
#define emoncms_apikey_LONG_NAME        "emoncms_apikey"
#define emoncms_server_LONG_NAME        "emoncms_server"
#define emoncms_node_LONG_NAME          "emoncms_node"
#define emoncms_fingerprint_LONG_NAME   "emoncms_fingerprint"
#define mqtt_server_LONG_NAME           "mqtt_server"
#define mqtt_topic_LONG_NAME            "mqtt_topic"
#define mqtt_user_LONG_NAME             "mqtt_user"
#define mqtt_pass_LONG_NAME             "mqtt_pass"
#define mqtt_solar_LONG_NAME            "mqtt_solar"
#define mqtt_grid_ie_LONG_NAME          "mqtt_grid_ie"
#define mqtt_port_LONG_NAME             "mqtt_port"
#define www_username_LONG_NAME          "www_username"
#define www_password_LONG_NAME          "www_password"
#define ohm_LONG_NAME                   "ohm"
#define flags_LONG_NAME                 "flags"
#define sntp_hostname_LONG_NAME         "sntp_hostname"
#define time_zone_LONG_NAME             "time_zone"

#define esp_hostname_SHORT_NAME         "hn"
#define esid_SHORT_NAME                 "ws"
#define epass_SHORT_NAME                "wp"
#define emoncms_apikey_SHORT_NAME       "ea"
#define emoncms_server_SHORT_NAME       "es"
#define emoncms_node_SHORT_NAME         "en"
#define emoncms_fingerprint_SHORT_NAME  "ef"
#define mqtt_server_SHORT_NAME          "ms"
#define mqtt_topic_SHORT_NAME           "mt"
#define mqtt_user_SHORT_NAME            "mu"
#define mqtt_pass_SHORT_NAME            "mp"
#define mqtt_solar_SHORT_NAME           "ms"
#define mqtt_grid_ie_SHORT_NAME         "mg"
#define mqtt_port_SHORT_NAME            "mpt"
#define www_username_SHORT_NAME         "au"
#define www_password_SHORT_NAME         "ap"
#define ohm_SHORT_NAME                  "o"
#define flags_SHORT_NAME                "f"
#define sntp_hostname_SHORT_NAME        "sh"
#define time_zone_SHORT_NAME            "tz"

bool config_deserialize(String& json);
bool config_deserialize(const char *json);

bool config_serialize(String& json, bool longNames = true);

#define EEPROM_SIZE                   4096

#define CHECKSUM_SEED 128

// -------------------------------------------------------------------
// Reset EEPROM, wipes all settings
// -------------------------------------------------------------------
void
ResetEEPROM() {
  EEPROM.begin(EEPROM_SIZE);

  //DEBUG.println("Erasing EEPROM");
  for (int i = 0; i < EEPROM_SIZE; ++i) {
    EEPROM.write(i, 0xff);
    //DEBUG.print("#");
  }
  EEPROM.end();
}

// -------------------------------------------------------------------
// Load saved settings from EEPROM
// -------------------------------------------------------------------
void
config_load_settings() 
{
  EEPROM.begin(EEPROM_SIZE);

  char start = 0;
  uint8_t a = 0, b = 0;
  EEPROM.get(0, a);
  EEPROM.get(1, b);
  int length = a | (b << 8);

  EEPROM.get(2, start);

  DBUGF("Got %d %c from EEPROM", length, start);

  if(2 <= length && length < EEPROM_SIZE &&
    '{' == start)
  {
    char json[length + 1];
    for(int i = 0; i < length; i++) {
      json[i] = EEPROM.read(2+i);
    }
    json[length] = '\0';
    DBUGF("Found stored JSON %s", json);
    config_deserialize(json);
    modified = false;
  } else {
    DBUGF("No JSON config found, trying v1 settings");
    config_load_v1_settings();
  }

  EEPROM.end();
}

void config_commit()
{
  if(false == modified) {
    return;
  }

  DBUGF("Saving config");
  
  EEPROM.begin(EEPROM_SIZE);

  String jsonStr;
  config_serialize(jsonStr, false);
  const char *json = jsonStr.c_str();
  DBUGF("Writing %s to EEPROM", json);
  int length = jsonStr.length();
  EEPROM.put(0, length & 0xff);
  EEPROM.put(1, (length >> 8) & 0xff);
  for(int i = 0; i < length; i++) {
    EEPROM.write(2+i, json[i]);
  }

  DBUGF("%d bytes written to EEPROM, committing", length + 2);

  if(EEPROM.commit())
  {
    DBUGF("Done");
    modified = false;
  } else {
    DBUGF("Writting EEPROM failed");
  }
}

#define GET_VALUE(val, def) do { \
  if(doc.containsKey(val ## _LONG_NAME)) { \
    val = (doc[val ## _LONG_NAME]); \
  } else if(doc.containsKey(val ## _SHORT_NAME)) { \
    val = (doc[val ## _SHORT_NAME]); \
  } else { \
    val = def; \
  }} while(false)

#define GET_VALUE_AS(type, val, def) do { \
  if(doc.containsKey(val ## _LONG_NAME)) { \
    val = (doc[val ## _LONG_NAME].as<type>()); \
  } else if(doc.containsKey(val ## _SHORT_NAME)) { \
    val = (doc[val ## _SHORT_NAME].as<type>()); \
  } else { \
    val = def; \
  }} while(false)

bool config_deserialize(String& json) {
  return config_deserialize(json.c_str());
}

bool config_deserialize(const char *json) 
{
  const size_t capacity = JSON_OBJECT_SIZE(30) + EEPROM_SIZE;
  DynamicJsonDocument doc(capacity);
  
  DeserializationError err = deserializeJson(doc, json);
  if(DeserializationError::Code::Ok == err)
  {
    // Device Hostname, needs to be read first as other config defaults depend on it
    GET_VALUE_AS(String, esp_hostname, esp_hostname_default);

    // Load WiFi values
    GET_VALUE_AS(String, esid, "");
    GET_VALUE_AS(String, epass, "");

    // EmonCMS settings
    GET_VALUE_AS(String, emoncms_apikey, "");
    GET_VALUE_AS(String, emoncms_server, "https://data.openevse.com/emoncms");
    GET_VALUE_AS(String, emoncms_node, esp_hostname);
    GET_VALUE_AS(String, emoncms_fingerprint, "");

    // MQTT settings
    GET_VALUE_AS(String, mqtt_server, "emonpi");
    GET_VALUE_AS(String, mqtt_topic, esp_hostname);
    GET_VALUE_AS(String, mqtt_user, "emonpi");
    GET_VALUE_AS(String, mqtt_pass, "emonpimqtt2016");
    GET_VALUE_AS(String, mqtt_solar, "");
    GET_VALUE_AS(String, mqtt_grid_ie, "emon/emonpi/power1");
    GET_VALUE(mqtt_port, 1883);

    // Web server credentials
    GET_VALUE_AS(String, www_username, "");
    GET_VALUE_AS(String, www_password, "");

    // Ohm Connect Settings
    GET_VALUE_AS(String, ohm, "");

    // Flags
    GET_VALUE(flags, CONFIG_SERVICE_SNTP);

    // Advanced
    GET_VALUE_AS(String, sntp_hostname, SNTP_DEFAULT_HOST);

    // Timezone
    GET_VALUE_AS(String, time_zone, DEFAULT_TIME_ZONE);
    config_set_timezone(time_zone);

    return true;
  }

  return false;
}

#undef GET_VALUE
#undef GET_VALUE_AS

#define SET_VALUE(val) \
  doc[(longNames ? val ## _LONG_NAME : val ## _SHORT_NAME)] = val

bool config_serialize(String& json, bool longNames)
{
  const size_t capacity = JSON_OBJECT_SIZE(30) + EEPROM_SIZE;
  DynamicJsonDocument doc(capacity);

  SET_VALUE(esp_hostname);

  // Load WiFi values
  SET_VALUE(esid);
  SET_VALUE(epass);

  // EmonCMS settings
  SET_VALUE(emoncms_apikey);
  SET_VALUE(emoncms_server);
  SET_VALUE(emoncms_node);
  SET_VALUE(emoncms_fingerprint);

  // MQTT settings
  SET_VALUE(mqtt_server);
  SET_VALUE(mqtt_topic);
  SET_VALUE(mqtt_user);
  SET_VALUE(mqtt_pass);
  SET_VALUE(mqtt_solar);
  SET_VALUE(mqtt_grid_ie);
  SET_VALUE(mqtt_port);

  // Web server credentials
  SET_VALUE(www_username);
  SET_VALUE(www_password);

  // Ohm Connect Settings
  SET_VALUE(ohm);

  // Flags
  SET_VALUE(flags);

  // Advanced
  SET_VALUE(sntp_hostname);

  // Timezone
  SET_VALUE(time_zone);

  serializeJson(doc, json);

  return true;
}

#undef SET_VALUE

void config_save_emoncms(bool enable, String server, String node, String apikey,
                    String fingerprint)
{
  uint32_t newflags = flags & ~CONFIG_SERVICE_EMONCMS;
  if(enable) {
    newflags |= CONFIG_SERVICE_EMONCMS;
  }

  config_set_emoncms_server(server);
  config_set_emoncms_node(node);
  config_set_emoncms_apikey(apikey);
  config_set_emoncms_fingerprint(fingerprint);
  config_set_flags(newflags);
  config_commit();
}

void
config_save_mqtt(bool enable, int protocol, String server, uint16_t port, String topic, String user, String pass, String solar, String grid_ie, bool reject_unauthorized)
{
  uint32_t newflags = flags & ~(CONFIG_SERVICE_MQTT | CONFIG_MQTT_PROTOCOL | CONFIG_MQTT_ALLOW_ANY_CERT);
  if(enable) {
    newflags |= CONFIG_SERVICE_MQTT;
  }
  if(!reject_unauthorized) {
    newflags |= CONFIG_MQTT_ALLOW_ANY_CERT;
  }
  newflags |= protocol << 4;  

  config_set_mqtt_server(server);
  config_set_mqtt_port(port);
  config_set_mqtt_topic(topic);
  config_set_mqtt_user(user);
  config_set_mqtt_pass(pass);
  config_set_mqtt_solar(solar);
  config_set_mqtt_grid_ie(grid_ie);
  config_set_flags(newflags);
  config_commit();
}

void
config_save_admin(String user, String pass) {
  config_set_www_username(user);
  config_set_www_password(pass);
  config_commit();
}

void
config_save_sntp(bool sntp_enable, String tz) 
{
  uint32_t newflags = flags & ~CONFIG_SERVICE_SNTP;
  if(sntp_enable) {
    newflags |= CONFIG_SERVICE_SNTP;
  }

  config_set_time_zone(tz);
  config_set_flags(newflags);
  config_commit();

  config_set_timezone(tz);
}

void config_set_timezone(String tz)
{
  const char *set_tz = tz.c_str();
  const char *split_pos = strchr(set_tz, '|');
  if(split_pos) {
    set_tz = split_pos;
  }

  setenv("TZ", set_tz, 1);
  tzset();
}

void
config_save_advanced(String hostname, String sntp_host) {
  config_set_esp_hostname(hostname);
  config_set_sntp_hostname(sntp_host);
  config_commit();
}

void
config_save_wifi(String qsid, String qpass)
{
  config_set_esid(qsid);
  config_set_epass(qpass);
  config_commit();
}

void
config_save_ohm(bool enable, String qohm)
{
  uint32_t newflags = flags & ~CONFIG_SERVICE_OHM;
  if(enable) {
    newflags |= CONFIG_SERVICE_OHM;
  }

  config_set_ohm(qohm);
  config_set_flags(newflags);
  config_commit();
}

void
config_save_flags(uint32_t newFlags) {
  config_set_flags(newFlags);
  config_commit();
}

void
config_reset() {
  ResetEEPROM();
}
