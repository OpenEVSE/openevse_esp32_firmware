#include "emonesp.h"
#include "espal.h"
#include "divert.h"
#include "mqtt.h"
#include "tesla_client.h"
#include "emoncms.h"
#include "input.h"

#include "app_config.h"
#include "app_config_mqtt.h"
#include "app_config_mode.h"

#include <Arduino.h>
#include <EEPROM.h>             // Save config settings
#include <ConfigJson.h>

#define EEPROM_SIZE     4096
#define CHECKSUM_SEED    128

// Wifi Network Strings
String esid;

// passphrase for Client mode
String epass;

// passphrase for Access Point mode
String apass;

// Web server authentication (leave blank for none)
String www_username;
String www_password;

// Advanced settings
String esp_hostname;
String sntp_hostname;

// EMONCMS SERVER strings
String emoncms_server;
String emoncms_node;
String emoncms_apikey;
String emoncms_fingerprint;

// MQTT Settings
String mqtt_server;
uint32_t mqtt_port;
String mqtt_topic;
String mqtt_user;
String mqtt_pass;
String mqtt_solar;
String mqtt_grid_ie;
String mqtt_vrms;
String mqtt_announce_topic;

// Time
String time_zone;

// 24-bits of Flags
uint32_t flags;

// Ohm Connect Settings
String ohm;

// Divert settings
double divert_PV_ratio;
double divert_attack_smoothing_factor;
double divert_decay_smoothing_factor;
uint32_t divert_min_charge_time;

// Tesla Client settings
String tesla_username;
String tesla_password;
int tesla_vehidx;

String esp_hostname_default = "openevse-"+ESPAL.getShortId();

void config_changed(String name);

ConfigOptDefenition<uint32_t> flagsOpt = ConfigOptDefenition<uint32_t>(flags, CONFIG_SERVICE_SNTP, "flags", "f");

ConfigOpt *opts[] = 
{
// Wifi Network Strings
  new ConfigOptDefenition<String>(esid, "", "ssid", "ws"),
  new ConfigOptSecret(epass, "", "pass", "wp"),
  new ConfigOptDefenition<String>(apass, "openevse", "apass", "apass"),

// Web server authentication (leave blank for none)
  new ConfigOptDefenition<String>(www_username, "", "www_username", "au"),
  new ConfigOptSecret(www_password, "", "www_password", "ap"),

// Advanced settings
  new ConfigOptDefenition<String>(esp_hostname, esp_hostname_default, "hostname", "hn"),
  new ConfigOptDefenition<String>(sntp_hostname, SNTP_DEFAULT_HOST, "sntp_hostname", "sh"),

// Time
  new ConfigOptDefenition<String>(time_zone, "", "time_zone", "tz"),

// EMONCMS SERVER strings
  new ConfigOptDefenition<String>(emoncms_server, "https://data.openevse.com/emoncms", "emoncms_server", "es"),
  new ConfigOptDefenition<String>(emoncms_node, esp_hostname, "emoncms_node", "en"),
  new ConfigOptSecret(emoncms_apikey, "", "emoncms_apikey", "ea"),
  new ConfigOptDefenition<String>(emoncms_fingerprint, "", "emoncms_fingerprint", "ef"),

// MQTT Settings
  new ConfigOptDefenition<String>(mqtt_server, "emonpi", "mqtt_server", "ms"),
  new ConfigOptDefenition<uint32_t>(mqtt_port, 1883, "mqtt_port", "mpt"),
  new ConfigOptDefenition<String>(mqtt_topic, esp_hostname, "mqtt_topic", "mt"),
  new ConfigOptDefenition<String>(mqtt_user, "emonpi", "mqtt_user", "mu"),
  new ConfigOptSecret(mqtt_pass, "emonpimqtt2016", "mqtt_pass", "mp"),
  new ConfigOptDefenition<String>(mqtt_solar, "", "mqtt_solar", "mo"),
  new ConfigOptDefenition<String>(mqtt_grid_ie, "emon/emonpi/power1", "mqtt_grid_ie", "mg"),
  new ConfigOptDefenition<String>(mqtt_vrms, "emon/emonpi/vrms", "mqtt_vrms", "mv"),
  new ConfigOptDefenition<String>(mqtt_announce_topic, "openevse/announce/"+ESPAL.getShortId(), "mqtt_announce_topic", "ma"),

// Ohm Connect Settings
  new ConfigOptDefenition<String>(ohm, "", "ohm", "o"),

// Divert settings
  new ConfigOptDefenition<double>(divert_PV_ratio, 1.1, "divert_PV_ratio", "dpr"),
  new ConfigOptDefenition<double>(divert_attack_smoothing_factor, 0.4, "divert_attack_smoothing_factor", "da"),
  new ConfigOptDefenition<double>(divert_decay_smoothing_factor, 0.05, "divert_decay_smoothing_factor", "dd"),
  new ConfigOptDefenition<uint32_t>(divert_min_charge_time, (10 * 60), "divert_min_charge_time", "dt"),

// Tesla client settings
  new ConfigOptDefenition<String>(tesla_username, "", "tesla_username", "tu"),
  new ConfigOptSecret(tesla_password, "", "tesla_password", "tp"),
  new ConfigOptDefenition<int>(tesla_vehidx, -1, "tesla_vehidx", "ti"),

// Flags
  &flagsOpt,

// Virtual Options
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_EMONCMS, CONFIG_SERVICE_EMONCMS, "emoncms_enabled", "ee"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_MQTT, CONFIG_SERVICE_MQTT, "mqtt_enabled", "me"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_MQTT_ALLOW_ANY_CERT, 0, "mqtt_reject_unauthorized", "mru"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_OHM, CONFIG_SERVICE_OHM, "ohm_enabled", "oe"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_SNTP, CONFIG_SERVICE_SNTP, "sntp_enabled", "se"),
  new ConfigOptVirtualBool(flagsOpt,CONFIG_SERVICE_TESLA,CONFIG_SERVICE_TESLA, "tesla_enabled", "te"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_DIVERT, CONFIG_SERVICE_DIVERT, "divert_enabled", "de"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_PAUSE_USES_DISABLED, CONFIG_PAUSE_USES_DISABLED, "pause_uses_disabled", "pd"),
  new ConfigOptVirtualMqttProtocol(flagsOpt, "mqtt_protocol", "mprt"),
  new ConfigOptVirtualChargeMode(flagsOpt, "charge_mode", "chmd")
};

ConfigJson config(opts, sizeof(opts) / sizeof(opts[0]), EEPROM_SIZE);

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
  config.onChanged(config_changed);

  if(!config.load()) {
    DBUGF("No JSON config found, trying v1 settings");
    config_load_v1_settings();
  }
}

void config_changed(String name)
{
  DBUGF("%s changed", name.c_str());

  if(name == "time_zone") {
    config_set_timezone(time_zone);
  } else if(name == "flags") {
    divertmode_update((config_divert_enabled() && 1 == config_charge_mode()) ? DIVERT_MODE_ECO : DIVERT_MODE_NORMAL);
    if(mqtt_connected() != config_mqtt_enabled()) {
      mqtt_restart();
    }
    if(emoncms_connected != config_emoncms_enabled()) {
      emoncms_updated = true;
    }
  } else if(name.startsWith("mqtt_")) {
    mqtt_restart();
  } else if(name.startsWith("emoncms_")) {
    emoncms_updated = true;
  } else if(name == "divert_enabled" || name == "charge_mode") {
    DBUGVAR(config_divert_enabled());
    DBUGVAR(config_charge_mode());
    divertmode_update((config_divert_enabled() && 1 == config_charge_mode()) ? DIVERT_MODE_ECO : DIVERT_MODE_NORMAL);
  } else if(name == "tesla_username") {
    teslaClient.setUser(tesla_username.c_str());
  } else if(name == "tesla_password") {
    teslaClient.setPass(tesla_password.c_str());
  } else if(name == "tesla_vehidx") {
    teslaClient.setVehicleIdx(tesla_vehidx);
  }
}

void config_commit()
{
  config.commit();
}

bool config_deserialize(String& json) {
  return config.deserialize(json.c_str());
}

bool config_deserialize(const char *json)
{
  return config.deserialize(json);
}

bool config_deserialize(DynamicJsonDocument &doc) 
{
  return config.deserialize(doc);
}

bool config_serialize(String& json, bool longNames, bool compactOutput, bool hideSecrets)
{
  return config.serialize(json, longNames, compactOutput, hideSecrets);
}

bool config_serialize(DynamicJsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets)
{
  return config.serialize(doc, longNames, compactOutput, hideSecrets);
}

void config_set(const char *name, uint32_t val) {
  config.set(name, val);
} 
void config_set(const char *name, String val) {
  config.set(name, val);
} 
void config_set(const char *name, bool val) {
  config.set(name, val);
} 
void config_set(const char *name, double val) {
  config.set(name, val);
} 

void config_save_emoncms(bool enable, String server, String node, String apikey,
                    String fingerprint)
{
  uint32_t newflags = flags & ~CONFIG_SERVICE_EMONCMS;
  if(enable) {
    newflags |= CONFIG_SERVICE_EMONCMS;
  }

  config.set("emoncms_server", server);
  config.set("emoncms_node", node);
  config.set("emoncms_apikey", apikey);
  config.set("emoncms_fingerprint", fingerprint);
  config.set("flags", newflags);
  config.commit();
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

  config.set("mqtt_server", server);
  config.set("mqtt_port", port);
  config.set("mqtt_topic", topic);
  config.set("mqtt_user", user);
  config.set("mqtt_pass", pass);
  config.set("mqtt_solar", solar);
  config.set("mqtt_grid_ie", grid_ie);
  config.set("flags", newflags);
  config.commit();
}

void
config_save_admin(String user, String pass) {
  config.set("www_username", user);
  config.set("www_password", pass);
  config.commit();
}

void
config_save_sntp(bool sntp_enable, String tz) 
{
  uint32_t newflags = flags & ~CONFIG_SERVICE_SNTP;
  if(sntp_enable) {
    newflags |= CONFIG_SERVICE_SNTP;
  }

  config.set("time_zone", tz);
  config.set("flags", newflags);
  config.commit();

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
  config.set("hostname", hostname);
  config.set("sntp_hostname", sntp_host);
  config.commit();
}

void
config_save_wifi(String qsid, String qpass)
{
  config.set("ssid", qsid);
  config.set("pass", qpass);
  config.commit();
}

void
config_save_ohm(bool enable, String qohm)
{
  uint32_t newflags = flags & ~CONFIG_SERVICE_OHM;
  if(enable) {
    newflags |= CONFIG_SERVICE_OHM;
  }

  config.set("ohm", qohm);
  config.set("flags", newflags);
  config.commit();
}

void
config_save_flags(uint32_t newFlags) {
  config.set("flags", newFlags);
  config.commit();
}

void
config_reset() {
  ResetEEPROM();
  config.reset();
}
