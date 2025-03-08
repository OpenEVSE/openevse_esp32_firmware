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

uint32_t config_ver = INITIAL_CONFIG_VERSION;

// Wifi Network Strings
String esid;
String epass;
String ap_ssid;
String ap_pass;

// Language
String lang;

// Web server authentication (leave blank for none)
String www_username;
String www_password;
String www_certificate_id;

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
String mqtt_certificate_id;
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
uint32_t flags_changed;

// Ohm Connect Settings
String ohm;

// Divert settings
int8_t divert_type;
double divert_PV_ratio;
uint32_t divert_attack_smoothing_time;
uint32_t divert_decay_smoothing_time;
uint32_t divert_min_charge_time;

// Current Shaper settings
uint32_t current_shaper_max_pwr;
uint32_t current_shaper_smoothing_time;
uint32_t current_shaper_min_pause_time;   // in seconds
uint32_t current_shaper_data_maxinterval; // in seconds

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

#define CONFIG_DEFAULT_FLAGS (CONFIG_SERVICE_SNTP | \
                              CONFIG_OCPP_AUTO_AUTH | \
                              CONFIG_OCPP_OFFLINE_AUTH | \
                              CONFIG_DEFAULT_STATE)

ConfigOptDefinition<uint32_t> flagsOpt = ConfigOptDefinition<uint32_t>(flags, CONFIG_DEFAULT_FLAGS, "flags", "f");
ConfigOptDefinition<uint32_t> flagsChanged = ConfigOptDefinition<uint32_t>(flags_changed, 0, "flags_changed", "c");

ConfigOpt *opts[] =
{
// Wifi Network Strings
  new ConfigOptDefinition<String>(esid, "", "ssid", "ws"),
  new ConfigOptSecret(epass, "", "pass", "wp"),
  new ConfigOptDefinition<String>(ap_ssid, "", "ap_ssid", "ss"),
  new ConfigOptSecret(ap_pass, "", "ap_pass", "sp"),

// Language String
  new ConfigOptDefinition<String>(lang, "", "lang", "lan"),

// Web server authentication (leave blank for none)
  new ConfigOptDefinition<String>(www_username, "", "www_username", "au"),
  new ConfigOptSecret(www_password, "", "www_password", "ap"),
  new ConfigOptDefenition<String>(www_certificate_id, "", "www_certificate_id", "wc"),

// Advanced settings
  new ConfigOptDefinition<String>(esp_hostname, esp_hostname_default, "hostname", "hn"),
  new ConfigOptDefinition<String>(sntp_hostname, SNTP_DEFAULT_HOST, "sntp_hostname", "sh"),

// Time
  new ConfigOptDefinition<String>(time_zone, DEFAULT_TIME_ZONE, "time_zone", "tz"),

// Limit
  new ConfigOptDefinition<String>(limit_default_type, {}, "limit_default_type", "ldt"),
  new ConfigOptDefinition<uint32_t>(limit_default_value, {}, "limit_default_value", "ldv"),

// EMONCMS SERVER strings
  new ConfigOptDefinition<String>(emoncms_server, "https://data.openevse.com/emoncms", "emoncms_server", "es"),
  new ConfigOptDefinition<String>(emoncms_node, esp_hostname, "emoncms_node", "en"),
  new ConfigOptSecret(emoncms_apikey, "", "emoncms_apikey", "ea"),
  new ConfigOptDefinition<String>(emoncms_fingerprint, "", "emoncms_fingerprint", "ef"),

// MQTT Settings
  new ConfigOptDefinition<String>(mqtt_server, "emonpi", "mqtt_server", "ms"),
  new ConfigOptDefinition<uint32_t>(mqtt_port, 1883, "mqtt_port", "mpt"),
  new ConfigOptDefinition<String>(mqtt_topic, esp_hostname, "mqtt_topic", "mt"),
  new ConfigOptDefinition<String>(mqtt_user, "emonpi", "mqtt_user", "mu"),
  new ConfigOptSecret(mqtt_pass, "emonpimqtt2016", "mqtt_pass", "mp"),
  new ConfigOptDefinition<String>(mqtt_certificate_id, "", "mqtt_certificate_id", "mct"),
  new ConfigOptDefinition<String>(mqtt_solar, "", "mqtt_solar", "mo"),
  new ConfigOptDefinition<String>(mqtt_grid_ie, "emon/emonpi/power1", "mqtt_grid_ie", "mg"),
  new ConfigOptDefinition<String>(mqtt_vrms, "emon/emonpi/vrms", "mqtt_vrms", "mv"),
  new ConfigOptDefinition<String>(mqtt_live_pwr, "", "mqtt_live_pwr", "map"),
  new ConfigOptDefinition<String>(mqtt_vehicle_soc, "", "mqtt_vehicle_soc", "mc"),
  new ConfigOptDefinition<String>(mqtt_vehicle_range, "", "mqtt_vehicle_range", "mr"),
  new ConfigOptDefinition<String>(mqtt_vehicle_eta, "", "mqtt_vehicle_eta", "met"),
  new ConfigOptDefinition<String>(mqtt_announce_topic, "openevse/announce/" + ESPAL.getShortId(), "mqtt_announce_topic", "ma"),

// OCPP 1.6 Settings
  new ConfigOptDefinition<String>(ocpp_server, "", "ocpp_server", "ows"),
  new ConfigOptDefinition<String>(ocpp_chargeBoxId, "", "ocpp_chargeBoxId", "cid"),
  new ConfigOptDefinition<String>(ocpp_authkey, "", "ocpp_authkey", "oky"),
  new ConfigOptDefinition<String>(ocpp_idtag, "DefaultIdTag", "ocpp_idtag", "idt"),

// Ohm Connect Settings
  new ConfigOptDefinition<String>(ohm, "", "ohm", "o"),

// Divert settings
  new ConfigOptDefinition<int8_t>(divert_type, -1, "divert_type", "dm"),
  new ConfigOptDefinition<double>(divert_PV_ratio, 1.1, "divert_PV_ratio", "dpr"),
  new ConfigOptDefinition<uint32_t>(divert_attack_smoothing_time, 20, "divert_attack_smoothing_time", "das"),
  new ConfigOptDefinition<uint32_t>(divert_decay_smoothing_time, 600, "divert_decay_smoothing_time", "dds"),
  new ConfigOptDefinition<uint32_t>(divert_min_charge_time, 600, "divert_min_charge_time", "dt"),

// Current Shaper settings
  new ConfigOptDefinition<uint32_t>(current_shaper_max_pwr, 0, "current_shaper_max_pwr", "smp"),
  new ConfigOptDefinition<uint32_t>(current_shaper_smoothing_time, 60, "current_shaper_smoothing_time", "sst"),
  new ConfigOptDefinition<uint32_t>(current_shaper_min_pause_time, 300, "current_shaper_min_pause_time", "spt"),
  new ConfigOptDefinition<uint32_t>(current_shaper_data_maxinterval, 120, "current_shaper_data_maxinterval", "sdm"),

// Vehicle settings
  new ConfigOptDefinition<uint8_t>(vehicle_data_src, 0, "vehicle_data_src", "vds"),

// Tesla client settings
  new ConfigOptSecret(tesla_access_token, "", "tesla_access_token", "tat"),
  new ConfigOptSecret(tesla_refresh_token, "", "tesla_refresh_token", "trt"),
  new ConfigOptDefinition<uint64_t>(tesla_created_at, -1, "tesla_created_at", "tc"),
  new ConfigOptDefinition<uint64_t>(tesla_expires_in, -1, "tesla_expires_in", "tx"),
  new ConfigOptDefinition<String>(tesla_vehicle_id, "", "tesla_vehicle_id", "ti"),

// RFID storage
  new ConfigOptDefinition<String>(rfid_storage, "", "rfid_storage", "rs"),

#if RGB_LED
// LED brightness
  new ConfigOptDefinition<uint8_t>(led_brightness, LED_DEFAULT_BRIGHTNESS, "led_brightness", "lb"),
#endif

// Scheduler options
  new ConfigOptDefinition<uint32_t>(scheduler_start_window, SCHEDULER_DEFAULT_START_WINDOW, "scheduler_start_window", "ssw"),

// Flags
  &flagsOpt,
  &flagsChanged,

// Virtual Options
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_SERVICE_EMONCMS, CONFIG_SERVICE_EMONCMS, "emoncms_enabled", "ee"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_SERVICE_MQTT, CONFIG_SERVICE_MQTT, "mqtt_enabled", "me"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_MQTT_ALLOW_ANY_CERT, 0, "mqtt_reject_unauthorized", "mru"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_MQTT_RETAINED, CONFIG_MQTT_RETAINED, "mqtt_retained", "mrt"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_SERVICE_OHM, CONFIG_SERVICE_OHM, "ohm_enabled", "oe"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_SERVICE_SNTP, CONFIG_SERVICE_SNTP, "sntp_enabled", "se"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_SERVICE_TESLA, CONFIG_SERVICE_TESLA, "tesla_enabled", "te"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_SERVICE_DIVERT, CONFIG_SERVICE_DIVERT, "divert_enabled", "de"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_SERVICE_CUR_SHAPER, CONFIG_SERVICE_CUR_SHAPER, "current_shaper_enabled", "cse"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_PAUSE_USES_DISABLED, CONFIG_PAUSE_USES_DISABLED, "pause_uses_disabled", "pd"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_VEHICLE_RANGE_MILES, CONFIG_VEHICLE_RANGE_MILES, "mqtt_vehicle_range_miles", "mvru"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_SERVICE_OCPP, CONFIG_SERVICE_OCPP, "ocpp_enabled", "ope"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_OCPP_AUTO_AUTH, CONFIG_OCPP_AUTO_AUTH, "ocpp_auth_auto", "oaa"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_OCPP_OFFLINE_AUTH, CONFIG_OCPP_OFFLINE_AUTH, "ocpp_auth_offline", "ooa"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_OCPP_ACCESS_SUSPEND, CONFIG_OCPP_ACCESS_SUSPEND, "ocpp_suspend_evse", "ops"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_OCPP_ACCESS_ENERGIZE, CONFIG_OCPP_ACCESS_ENERGIZE, "ocpp_energize_plug", "opn"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_RFID, CONFIG_RFID, "rfid_enabled", "rf"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_FACTORY_WRITE_LOCK, CONFIG_FACTORY_WRITE_LOCK, "factory_write_lock", "fwl"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_THREEPHASE, CONFIG_THREEPHASE, "is_threephase", "itp"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_WIZARD, CONFIG_WIZARD, "wizard_passed", "wzp"),
  new ConfigOptVirtualMaskedBool(flagsOpt, flagsChanged, CONFIG_DEFAULT_STATE, CONFIG_DEFAULT_STATE, "default_state", "dfs"),
  new ConfigOptVirtualMqttProtocol(flagsOpt, flagsChanged, "mqtt_protocol", "mprt"),
  new ConfigOptVirtualChargeMode(flagsOpt, flagsChanged, "charge_mode", "chmd")
};

ConfigJson user_config(opts, sizeof(opts) / sizeof(opts[0]), EEPROM_SIZE, CONFIG_OFFSET);
ConfigJson factory_config(opts, sizeof(opts) / sizeof(opts[0]), EEPROM_SIZE, FACTORY_OFFSET);

// -------------------------------------------------------------------
// config version handling
// -------------------------------------------------------------------
uint32_t
config_version() {
  return config_ver;
}

void
increment_config() {
  config_ver++;
  DBUGVAR(config_ver);

  #if ENABLE_CONFIG_CHANGE_NOTIFICATION
  StaticJsonDocument<128> event;
  event["config_version"] = config_ver;
  event_send(event);
  #endif
}

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

  // Handle setting the newly added flag changed bits
  if(0 == flags_changed)
  {
    // Assume all flags that do not match the default value have changed
    uint32_t new_changed = (flags ^ CONFIG_DEFAULT_FLAGS) & ~CONFIG_DEFAULT_STATE;

    // Handle the default charge state differently as that was set as a default to 0 previously, but is now 1
    // We will assume that if set to 1 it was intentional, but if 0 we will assume it was the just the default
    // and not an intentinal change
    if(flags != CONFIG_DEFAULT_FLAGS &&
       CONFIG_DEFAULT_STATE == (flags & CONFIG_DEFAULT_STATE))
    {
      new_changed |= CONFIG_DEFAULT_STATE;
    }

    // Save any changes
    if(flagsChanged.set(new_changed)) {
      user_config.commit();
    }
  }

  // now lets apply any default flags that have not explicitly been set by the user
  flags |= CONFIG_DEFAULT_FLAGS & ~flags_changed;
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
    OcppTask::notifyConfigChanged();
    evse.setSleepForDisable(!config_pause_uses_disabled());
  } else if(name.startsWith("mqtt_")) {
    mqtt_restart();
  } else if(name.startsWith("ocpp_")) {
    OcppTask::notifyConfigChanged();
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
  bool config_modified = user_config.deserialize(doc);

  #if ENABLE_CONFIG_CHANGE_NOTIFICATION
  // Update EVSE config
  // Update the EVSE setting flags, a little low level, may move later
  if(doc.containsKey("diode_check"))
  {
    bool enable = doc["diode_check"];
    if(enable != evse.isDiodeCheckEnabled()) {
      evse.enableDiodeCheck(enable);
      config_modified = true;
      DBUGLN("diode_check changed");
    }
  }

  if(doc.containsKey("gfci_check"))
  {
    bool enable = doc["gfci_check"];
    if(enable != evse.isGfiTestEnabled()) {
      evse.enableGfiTestCheck(enable);
      config_modified = true;
      DBUGLN("gfci_check changed");
    }
  }

  if(doc.containsKey("ground_check"))
  {
    bool enable = doc["ground_check"];
    if(enable != evse.isGroundCheckEnabled()) {
      evse.enableGroundCheck(enable);
      config_modified = true;
      DBUGLN("ground_check changed");
    }
  }

  if(doc.containsKey("relay_check"))
  {
    bool enable = doc["relay_check"];
    if(enable != evse.isStuckRelayCheckEnabled()) {
      evse.enableStuckRelayCheck(enable);
      config_modified = true;
      DBUGLN("relay_check changed");
    }
  }

  if(doc.containsKey("vent_check"))
  {
    bool enable = doc["vent_check"];
    if(enable != evse.isVentRequiredEnabled()) {
      evse.enableVentRequired(enable);
      config_modified = true;
      DBUGLN("vent_check changed");
    }
  }

  if(doc.containsKey("temp_check"))
  {
    bool enable = doc["temp_check"];
    if(enable != evse.isTemperatureCheckEnabled()) {
      evse.enableTemperatureCheck(enable);
      config_modified = true;
      DBUGLN("temp_check changed");
    }
  }

  if(doc.containsKey("service"))
  {
    EvseMonitor::ServiceLevel service = static_cast<EvseMonitor::ServiceLevel>(doc["service"].as<uint8_t>());
    if(service != evse.getServiceLevel()) {
      evse.setServiceLevel(service);
      config_modified = true;
      DBUGLN("service changed");
    }
  }

  if(doc.containsKey("max_current_soft"))
  {
    long current = doc["max_current_soft"];
    if(current != evse.getMaxConfiguredCurrent()) {
      evse.setMaxConfiguredCurrent(current);
      config_modified = true;
      DBUGLN("max_current_soft changed");
    }
  }

  if(doc.containsKey("scale") && doc.containsKey("offset"))
  {
    long scale = doc["scale"];
    long offset = doc["offset"];
    if(scale != evse.getCurrentSensorScale() || offset != evse.getCurrentSensorOffset()) {
      evse.configureCurrentSensorScale(doc["scale"], doc["offset"]);
      config_modified = true;
      DBUGLN("scale changed");
      evse.getAmmeterSettings();
    }
  }
  #endif

  if(config_modified)
  {
    #if ENABLE_CONFIG_CHANGE_NOTIFICATION
    // HACK: force a flush of the RAPI command queue to make sure the config
    //       is updated before we send the response
    DBUG("Flushing RAPI command queue ...");
    rapiSender.flush();
    DBUGLN(" Done");
    #endif

    increment_config();
  }

  return config_modified;
}

bool config_serialize(String& json, bool longNames, bool compactOutput, bool hideSecrets)
{
  return user_config.serialize(json, longNames, compactOutput, hideSecrets);
}

bool config_serialize(DynamicJsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets)
{
  // Static supported protocols
  JsonArray mqtt_supported_protocols = doc.createNestedArray("mqtt_supported_protocols");
  mqtt_supported_protocols.add("mqtt");
  mqtt_supported_protocols.add("mqtts");
  JsonArray http_supported_protocols = doc.createNestedArray("http_supported_protocols");
  http_supported_protocols.add("http");

  #if ENABLE_CONFIG_CHANGE_NOTIFICATION
  doc["buildenv"] = buildenv;
  doc["version"] = currentfirmware;
  doc["wifi_serial"] = serial;
  doc["protocol"] = "-";
  doc["espinfo"] = ESPAL.getChipInfo();
  doc["espflash"] = ESPAL.getFlashChipSize();

  // EVSE information are only evailable when config_version is incremented
  if(config_ver > 0) {
    // Read only information
    doc["firmware"] = evse.getFirmwareVersion();
    doc["evse_serial"] = evse.getSerial();
    // OpenEVSE module config
    doc["diode_check"] = evse.isDiodeCheckEnabled();
    doc["gfci_check"] = evse.isGfiTestEnabled();
    doc["ground_check"] = evse.isGroundCheckEnabled();
    doc["relay_check"] = evse.isStuckRelayCheckEnabled();
    doc["vent_check"] = evse.isVentRequiredEnabled();
    doc["temp_check"] = evse.isTemperatureCheckEnabled();
    doc["max_current_soft"] = evse.getMaxConfiguredCurrent();
    // OpenEVSE Read only information
    doc["service"] = static_cast<uint8_t>(evse.getServiceLevel());
    doc["scale"] = evse.getCurrentSensorScale();
    doc["offset"] = evse.getCurrentSensorOffset();
    doc["min_current_hard"] = evse.getMinCurrent();
    doc["max_current_hard"] = evse.getMaxHardwareCurrent();
  }
  #endif

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



