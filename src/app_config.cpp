#include "emonesp.h"
#include "espal.h"

#include <Arduino.h>
#include <EEPROM.h>             // Save config settings
#include <ConfigJson.h>
#include <LittleFS.h>

#include "app_config.h"
#include "app_config_mqtt.h"
#include "app_config_mode.h"

#if ENABLE_CONFIG_CHANGE_NOTIFICATION
#include "divert.h"
#include "net_manager.h"
#include "mqtt.h"
#include "ocpp.h"
#include "tesla_client.h"
#include "emoncms.h"
#include "input.h"
#include "LedManagerTask.h"
#include "current_shaper.h"
#include "limit.h"
#endif

#define EEPROM_SIZE       4096

#define CONFIG_OFFSET     0
#define CONFIG_SIZE       3072

#define FACTORY_OFFSET    CONFIG_SIZE
#define FACTORY_SIZE      1024

// Wifi Network Strings
String esid;
String epass;

// Language
String lang;

// Web server authentication (leave blank for none)
String www_username;
String www_password;

// Advanced settings
String esp_hostname;
String sntp_hostname;

// LIMIT Settings
String limit_default_type;
uint32_t limit_default_value;

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
String mqtt_live_pwr;
String mqtt_vehicle_soc;
String mqtt_vehicle_range;
String mqtt_vehicle_eta;
String mqtt_announce_topic;

// OCPP 1.6 Settings
String ocpp_server;
String ocpp_chargeBoxId;
String ocpp_authkey;
String ocpp_idtag;

// Time
String time_zone;

// 24-bits of Flags
uint32_t flags;

// Ohm Connect Settings
String ohm;

// Divert settings
int8_t divert_type;
double divert_PV_ratio;
double divert_attack_smoothing_factor;
double divert_decay_smoothing_factor;
uint32_t divert_min_charge_time;

// Current Shaper settings
uint32_t current_shaper_max_pwr;

// Tesla Client settings
String tesla_access_token;
String tesla_refresh_token;
uint64_t tesla_created_at;
uint64_t tesla_expires_in;

String tesla_vehicle_id;

// Vehicle
uint8_t vehicle_data_src;

#if RGB_LED
uint8_t led_brightness;
#endif

// RFID storage
String rfid_storage;

long max_current_soft;

// Scheduler settings
uint32_t scheduler_start_window;

String esp_hostname_default = "openevse-"+ESPAL.getShortId();

void config_changed(String name);

ConfigOptDefenition<uint32_t> flagsOpt = ConfigOptDefenition<uint32_t>(flags, CONFIG_SERVICE_SNTP, "flags", "f");

ConfigOpt *opts[] =
{
// Wifi Network Strings
  new ConfigOptDefenition<String>(esid, "", "ssid", "ws"),
  new ConfigOptSecret(epass, "", "pass", "wp"),

// Language String
  new ConfigOptDefenition<String>(lang, "", "lang", "lan"),

// Web server authentication (leave blank for none)
  new ConfigOptDefenition<String>(www_username, "", "www_username", "au"),
  new ConfigOptSecret(www_password, "", "www_password", "ap"),

// Advanced settings
  new ConfigOptDefenition<String>(esp_hostname, esp_hostname_default, "hostname", "hn"),
  new ConfigOptDefenition<String>(sntp_hostname, SNTP_DEFAULT_HOST, "sntp_hostname", "sh"),

// Time
  new ConfigOptDefenition<String>(time_zone, DEFAULT_TIME_ZONE, "time_zone", "tz"),

// Limit
  new ConfigOptDefenition<String>(limit_default_type, {}, "limit_default_type", "ldt"),
  new ConfigOptDefenition<uint32_t>(limit_default_value, {}, "limit_default_value", "ldv"),

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
  new ConfigOptDefenition<String>(mqtt_live_pwr, "", "mqtt_live_pwr", "map"),
  new ConfigOptDefenition<String>(mqtt_vehicle_soc, "", "mqtt_vehicle_soc", "mc"),
  new ConfigOptDefenition<String>(mqtt_vehicle_range, "", "mqtt_vehicle_range", "mr"),
  new ConfigOptDefenition<String>(mqtt_vehicle_eta, "", "mqtt_vehicle_eta", "met"),
  new ConfigOptDefenition<String>(mqtt_announce_topic, "openevse/announce/"+ESPAL.getShortId(), "mqtt_announce_topic", "ma"),

// OCPP 1.6 Settings
  new ConfigOptDefenition<String>(ocpp_server, "", "ocpp_server", "ows"),
  new ConfigOptDefenition<String>(ocpp_chargeBoxId, "", "ocpp_chargeBoxId", "cid"),
  new ConfigOptDefenition<String>(ocpp_authkey, "", "ocpp_authkey", "oky"),
  new ConfigOptDefenition<String>(ocpp_idtag, "", "ocpp_idtag", "idt"),

// Ohm Connect Settings
  new ConfigOptDefenition<String>(ohm, "", "ohm", "o"),

// Divert settings
  new ConfigOptDefenition<int8_t>(divert_type, -1, "divert_type", "dm"),
  new ConfigOptDefenition<double>(divert_PV_ratio, 1.1, "divert_PV_ratio", "dpr"),
  new ConfigOptDefenition<double>(divert_attack_smoothing_factor, 0.4, "divert_attack_smoothing_factor", "da"),
  new ConfigOptDefenition<double>(divert_decay_smoothing_factor, 0.05, "divert_decay_smoothing_factor", "dd"),
  new ConfigOptDefenition<uint32_t>(divert_min_charge_time, (10 * 60), "divert_min_charge_time", "dt"),

// Current Shaper settings
  new ConfigOptDefenition<uint32_t>(current_shaper_max_pwr, 0, "current_shaper_max_pwr", "smp"),

// Vehicle settings
  new ConfigOptDefenition<uint8_t>(vehicle_data_src, 0, "vehicle_data_src", "vds"),

// Tesla client settings
  new ConfigOptSecret(tesla_access_token, "", "tesla_access_token", "tat"),
  new ConfigOptSecret(tesla_refresh_token, "", "tesla_refresh_token", "trt"),
  new ConfigOptDefenition<uint64_t>(tesla_created_at, -1, "tesla_created_at", "tc"),
  new ConfigOptDefenition<uint64_t>(tesla_expires_in, -1, "tesla_expires_in", "tx"),
  new ConfigOptDefenition<String>(tesla_vehicle_id, "", "tesla_vehicle_id", "ti"),

// RFID storage
  new ConfigOptDefenition<String>(rfid_storage, "", "rfid_storage", "rs"),

#if RGB_LED
// LED brightness
  new ConfigOptDefenition<uint8_t>(led_brightness, LED_DEFAULT_BRIGHTNESS, "led_brightness", "lb"),
#endif

// Scheduler options
  new ConfigOptDefenition<uint32_t>(scheduler_start_window, SCHEDULER_DEFAULT_START_WINDOW, "scheduler_start_window", "ssw"),

// Flags
  &flagsOpt,

// Virtual Options
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_EMONCMS, CONFIG_SERVICE_EMONCMS, "emoncms_enabled", "ee"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_MQTT, CONFIG_SERVICE_MQTT, "mqtt_enabled", "me"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_MQTT_ALLOW_ANY_CERT, 0, "mqtt_reject_unauthorized", "mru"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_MQTT_RETAINED, CONFIG_MQTT_RETAINED, "mqtt_retained", "mrt"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_OHM, CONFIG_SERVICE_OHM, "ohm_enabled", "oe"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_SNTP, CONFIG_SERVICE_SNTP, "sntp_enabled", "se"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_TESLA, CONFIG_SERVICE_TESLA, "tesla_enabled", "te"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_DIVERT, CONFIG_SERVICE_DIVERT, "divert_enabled", "de"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_CUR_SHAPER, CONFIG_SERVICE_CUR_SHAPER, "current_shaper_enabled", "cse"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_PAUSE_USES_DISABLED, CONFIG_PAUSE_USES_DISABLED, "pause_uses_disabled", "pd"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_VEHICLE_RANGE_MILES, CONFIG_VEHICLE_RANGE_MILES, "mqtt_vehicle_range_miles", "mvru"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_OCPP, CONFIG_SERVICE_OCPP, "ocpp_enabled", "ope"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_OCPP_AUTO_AUTH, CONFIG_OCPP_AUTO_AUTH, "ocpp_auth_auto", "oaa"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_OCPP_OFFLINE_AUTH, CONFIG_OCPP_OFFLINE_AUTH, "ocpp_auth_offline", "ooa"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_OCPP_ACCESS_SUSPEND, CONFIG_OCPP_ACCESS_SUSPEND, "ocpp_suspend_evse", "ops"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_OCPP_ACCESS_ENERGIZE, CONFIG_OCPP_ACCESS_ENERGIZE, "ocpp_energize_plug", "opn"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_RFID, CONFIG_RFID, "rfid_enabled", "rf"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_FACTORY_WRITE_LOCK, CONFIG_FACTORY_WRITE_LOCK, "factory_write_lock", "fwl"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_THREEPHASE, CONFIG_THREEPHASE, "is_threephase", "itp"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_WIZARD, CONFIG_WIZARD, "wizard_passed", "wzp"),
  new ConfigOptVirtualMqttProtocol(flagsOpt, "mqtt_protocol", "mprt"),
  new ConfigOptVirtualChargeMode(flagsOpt, "charge_mode", "chmd")
  };

ConfigJson user_config(opts, sizeof(opts) / sizeof(opts[0]), EEPROM_SIZE, CONFIG_OFFSET);
ConfigJson factory_config(opts, sizeof(opts) / sizeof(opts[0]), EEPROM_SIZE, FACTORY_OFFSET);

// -------------------------------------------------------------------
// Reset EEPROM, wipes all settings
// -------------------------------------------------------------------
void
ResetEEPROM() {
  EEPROM.begin(EEPROM_SIZE);

  //DEBUG.println("Erasing EEPROM");
  for (int i = CONFIG_OFFSET; i < (CONFIG_OFFSET + CONFIG_SIZE); ++i) {
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
  user_config.onChanged(config_changed);

  factory_config.load(false);
  if(!user_config.load(true))
  {
#if ENABLE_CONFIG_V1_IMPORT
    DBUGF("No JSON config found, trying v1 settings");
    config_load_v1_settings();
#else
    DBUGF("No JSON config found, using defaults");
#endif
  }
}

void config_changed(String name)
{
  DBUGF("%s changed", name.c_str());

#if ENABLE_CONFIG_CHANGE_NOTIFICATION
  if(name == "time_zone") {
    timeManager.setTimeZone(time_zone);
  } else if(name == "flags") {
    divert.setMode((config_divert_enabled() && 1 == config_charge_mode()) ? DivertMode::Eco : DivertMode::Normal);
    if(mqtt_connected() != config_mqtt_enabled()) {
      mqtt_restart();
    }
    if(emoncms_connected != config_emoncms_enabled()) {
      emoncms_updated = true;
    }
    timeManager.setSntpEnabled(config_sntp_enabled());
    ArduinoOcppTask::notifyConfigChanged();
    evse.setSleepForDisable(!config_pause_uses_disabled());
  } else if(name.startsWith("mqtt_")) {
    mqtt_restart();
  } else if(name.startsWith("ocpp_")) {
    ArduinoOcppTask::notifyConfigChanged();
  } else if(name.startsWith("emoncms_")) {
    emoncms_updated = true;
  } else if(name.startsWith("scheduler_")) {
    scheduler.notifyConfigChanged();
  } else if(name == "divert_enabled" || name == "charge_mode") {
    DBUGVAR(config_divert_enabled());
    DBUGVAR(config_charge_mode());
    divert.setMode((config_divert_enabled() && 1 == config_charge_mode()) ? DivertMode::Eco : DivertMode::Normal);
  } else if(name.startsWith("current_shaper_")) {
    shaper.notifyConfigChanged(config_current_shaper_enabled()?1:0,current_shaper_max_pwr);
  } else if(name == "tesla_vehicle_id") {
    teslaClient.setVehicleId(tesla_vehicle_id);
  } else if(name.startsWith("tesla_")) {
    teslaClient.setCredentials(tesla_access_token, tesla_refresh_token, tesla_created_at, tesla_expires_in);
#if RGB_LED
  } else if(name == "led_brightness") {
    ledManager.setBrightness(led_brightness);
#endif
  } else if(name.startsWith("limit_default_")) {
    LimitProperties limitprops;
    LimitType limitType;
    DBUGVAR(limit_default_type);
    DBUGVAR((int)limit_default_value);
    limitType.fromString(limit_default_type.c_str());
    limitprops.setType(limitType);
    limitprops.setValue(limit_default_value);
    limitprops.setAutoRelease(false);
    if (limitType == LimitType::None) {
      limit.clear();
      DBUGLN("No limit to set");
    }
    else if (limitprops.getValue())
      limit.set(limitprops);
    DBUGLN("Limit set");
    DBUGVAR(limitprops.getType().toString());
    DBUGVAR(limitprops.getValue());
  } else if(name == "sntp_enabled") {
    timeManager.setSntpEnabled(config_sntp_enabled());
  }
#endif
}

void config_commit(bool factory)
{
  ConfigJson &config = factory ? factory_config : user_config;
  config.set("factory_write_lock", true);
  config.commit();
}

bool config_deserialize(String& json) {
  return user_config.deserialize(json.c_str());
}

bool config_deserialize(const char *json)
{
  return user_config.deserialize(json);
}

bool config_deserialize(DynamicJsonDocument &doc)
{
  return user_config.deserialize(doc);
}

bool config_serialize(String& json, bool longNames, bool compactOutput, bool hideSecrets)
{
  return user_config.serialize(json, longNames, compactOutput, hideSecrets);
}

bool config_serialize(DynamicJsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets)
{
  return user_config.serialize(doc, longNames, compactOutput, hideSecrets);
}

void config_set(const char *name, uint32_t val) {
  user_config.set(name, val);
}
void config_set(const char *name, String val) {
  user_config.set(name, val);
}
void config_set(const char *name, bool val) {
  user_config.set(name, val);
}
void config_set(const char *name, double val) {
  user_config.set(name, val);
}

void config_reset()
{
  ResetEEPROM();
  LittleFS.format();
  config_load_settings();
}



