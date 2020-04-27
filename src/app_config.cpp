#include "emonesp.h"
#include "app_config.h"
#include "hal.h"
#include "divert.h"

#include <Arduino.h>
#include <EEPROM.h>             // Save config settings
#include <ArduinoJson.h>

bool modified = false;

// Wifi Network Strings
String esid;
String epass;

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
String mqtt_announce_topic;

// Time
String time_zone;

// 24-bits of Flags
uint32_t flags;

// Ohm Connect Settings
String ohm;

// Divert settings
double divert_attack_smoothing_factor;
double divert_decay_smoothing_factor;
uint32_t divert_min_charge_time;

String esp_hostname_default = "openevse-"+HAL.getShortId();

static const char _DUMMY_PASSWORD[] PROGMEM = "_DUMMY_PASSWORD";
#define DUMMY_PASSWORD FPSTR(_DUMMY_PASSWORD)

void config_changed(const char *name);

class ConfigOpt;
template<class T> class ConfigOptDefenition;

class ConfigOpt
{
protected:
  const char *_long;
  const char *_short;
public:
  ConfigOpt(const char *l, const char *s) :
    _long(l),
    _short(s)
  {
  }

  const char *name(bool longName = true) {
    return longName ? _long : _short;
  }

  virtual void serialize(DynamicJsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets) = 0;
  virtual void deserialize(DynamicJsonDocument &doc) = 0;
  virtual void setDefault() = 0;

  template <typename T> void set(T val) {
    ConfigOptDefenition<T> *opt = (ConfigOptDefenition<T> *)this;
    opt->set(val);
  }
};

template<class T>
class ConfigOptDefenition : public ConfigOpt
{
protected:
  T &_val;
  T _default;

public:
  ConfigOptDefenition(T &v, T d, const char *l, const char *s) :
    ConfigOpt(l, s),
    _val(v),
    _default(d)
  {
  }

  T get() {
    return _val;
  }

  virtual void set(T value) {    
    if(_val != value) {
      DBUG(_long);
      DBUG(" set to ");
      DBUGLN(value);
      _val = value;
      modified = true;
      config_changed(_long);
    }
  }

  virtual void serialize(DynamicJsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets) {
    if(!compactOutput || _val != _default) {
      doc[name(longNames)] = _val;
    }
  }

  virtual void deserialize(DynamicJsonDocument &doc) {
    if(doc.containsKey(_long)) {
      T val = doc[_long].as<T>();
      set(val);
    } else if(doc.containsKey(_short)) {
      T val = doc[_short].as<T>();
      set(val);
    }
  }

  virtual void setDefault() {
    _val = _default;
  }
};

class ConfigOptSecret : public ConfigOptDefenition<String>
{
public:
  ConfigOptSecret(String &v, String d, const char *l, const char *s) :
    ConfigOptDefenition<String>(v, d, l, s)
  {
  }

  void set(String value) {
    if(value.equals(DUMMY_PASSWORD)) {
      return;
    }

    ConfigOptDefenition<String>::set(value);
  }

  virtual void serialize(DynamicJsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets) {
    if(!compactOutput || _val != _default) {
      if(hideSecrets) {
        doc[name(longNames)] = (_val != 0) ? String(DUMMY_PASSWORD) : "";
      } else {
        doc[name(longNames)] = _val;
      }
    }
  }
};

class ConfigOptVirtualBool : public ConfigOpt
{
protected:
  ConfigOptDefenition<uint32_t> &_base;
  uint32_t _mask;
  uint32_t _expected;

public:
  ConfigOptVirtualBool(ConfigOptDefenition<uint32_t> &b, uint32_t m, uint32_t e, const char *l, const char *s) :
    ConfigOpt(l, s),
    _base(b),
    _mask(m),
    _expected(e)
  {
  }

  bool get() {
    return _expected == (_base.get() & _mask);
  }

  virtual void set(bool value) {
    uint32_t newVal = _base.get() & ~(_mask);
    if(value == (_mask == _expected)) {
      newVal |= _mask;
    }
    _base.set(newVal);
  }

  virtual void serialize(DynamicJsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets) {
    if(!compactOutput) {
      doc[name(longNames)] = get();
    }
  }

  virtual void deserialize(DynamicJsonDocument &doc) {
    if(doc.containsKey(_long)) {
      set(doc[_long].as<bool>());
    } else if(doc.containsKey(_short)) { \
      set(doc[_short].as<bool>());
    }
  }

  virtual void setDefault() {
  }
};

class ConfigOptVirtualMqttProtocol : public ConfigOpt
{
protected:
  ConfigOptDefenition<uint32_t> &_base;

public:
  ConfigOptVirtualMqttProtocol(ConfigOptDefenition<uint32_t> &b, const char *l, const char *s) :
    ConfigOpt(l, s),
    _base(b)
  {
  }

  String get() {
    int protocol = (_base.get() & CONFIG_MQTT_PROTOCOL) >> 4;
    return 0 == protocol ? "mqtt" : "mqtts";
  }

  virtual void set(String value) {
    uint32_t newVal = _base.get() & ~CONFIG_MQTT_PROTOCOL;
    if(value == "mqtts") {
      newVal |= 1;
    }
    _base.set(newVal);
  }

  virtual void serialize(DynamicJsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets) {
    if(!compactOutput) {
      doc[name(longNames)] = get();
    }
  }

  virtual void deserialize(DynamicJsonDocument &doc) {
    if(doc.containsKey(_long)) {
      set(doc[_long].as<String>());
    } else if(doc.containsKey(_short)) { \
      set(doc[_short].as<String>());
    }
  }

  virtual void setDefault() {
  }
};

ConfigOptDefenition<uint32_t> flagsOpt = ConfigOptDefenition<uint32_t>(flags, CONFIG_SERVICE_SNTP, "flags", "f");

ConfigOpt *opts[] = 
{
// Wifi Network Strings
  new ConfigOptDefenition<String>(esid, "", "ssid", "ws"),
  new ConfigOptSecret(epass, "", "pass", "wp"),

// Web server authentication (leave blank for none)
  new ConfigOptDefenition<String>(www_username, "", "www_username", "au"),
  new ConfigOptSecret(www_password, "", "www_password", "ap"),

// Advanced settings
  new ConfigOptDefenition<String>(esp_hostname, esp_hostname_default, "esp_hostname", "hn"),
  new ConfigOptDefenition<String>(sntp_hostname, SNTP_DEFAULT_HOST, "sntp_hostname", "sh"),

// Time
  new ConfigOptDefenition<String>(time_zone, "", "time_zone", "tz"),

// EMONCMS SERVER strings
  new ConfigOptDefenition<String>(emoncms_server, "https://data.openevse.com/emoncms", "emoncms_server", "es"),
  new ConfigOptDefenition<String>(emoncms_node, esp_hostname_default, "emoncms_node", "en"),
  new ConfigOptSecret(emoncms_apikey, "", "emoncms_apikey", "ea"),
  new ConfigOptDefenition<String>(emoncms_fingerprint, "", "emoncms_fingerprint", "ef"),

// MQTT Settings
  new ConfigOptDefenition<String>(mqtt_server, "emonpi", "mqtt_server", "ms"),
  new ConfigOptDefenition<uint32_t>(mqtt_port, 1883, "mqtt_port", "mpt"),
  new ConfigOptDefenition<String>(mqtt_topic, "", "mqtt_topic", "mt"),
  new ConfigOptDefenition<String>(mqtt_user, "emonpi", "mqtt_user", "mu"),
  new ConfigOptSecret(mqtt_pass, "emonpimqtt2016", "mqtt_pass", "mp"),
  new ConfigOptDefenition<String>(mqtt_solar, "", "mqtt_solar", "mo"),
  new ConfigOptDefenition<String>(mqtt_grid_ie, "emon/emonpi/power1", "mqtt_grid_ie", "mg"),
  new ConfigOptDefenition<String>(mqtt_announce_topic, "openevse/announce/"+HAL.getShortId(), "mqtt_announce_topic", "ma"),

// Ohm Connect Settings
  new ConfigOptDefenition<String>(ohm, "", "ohm", "o"),

// Divert settings
  new ConfigOptDefenition<double>(divert_attack_smoothing_factor, 0.4, "divert_attack_smoothing_factor", "da"),
  new ConfigOptDefenition<double>(divert_decay_smoothing_factor, 0.05, "divert_decay_smoothing_factor", "dd"),
  new ConfigOptDefenition<uint32_t>(divert_min_charge_time, (10 * 60), "divert_min_charge_time", "dt"),

// Flags
  &flagsOpt,

// Virtual Options
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_EMONCMS, CONFIG_SERVICE_EMONCMS, "emoncms_enabled", "ee"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_MQTT, CONFIG_SERVICE_MQTT, "mqtt_enabled", "me"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_MQTT_ALLOW_ANY_CERT, 0, "mqtt_reject_unauthorized", "mru"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_OHM, CONFIG_SERVICE_OHM, "ohm_enabled", "oe"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_SNTP, CONFIG_SERVICE_SNTP, "sntp_enabled", "se"),
  new ConfigOptVirtualBool(flagsOpt, CONFIG_SERVICE_DIVERT, CONFIG_SERVICE_DIVERT, "divert_enabled", "de"),
  new ConfigOptVirtualMqttProtocol(flagsOpt, "mqtt_protocol", "mprt")
};

const size_t opts_length = sizeof(opts) / sizeof(opts[0]);

void config_set_defaults();

template <typename T> void config_set(const char *name, T val);

#define EEPROM_SIZE     4096
#define CHECKSUM_SEED    128

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
  config_set_defaults();

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

  modified = false;
}

void config_changed(const char *name)
{
  if(0 == strcmp(name, "time_zone")) {
    config_set_timezone(time_zone);
  } else if(0 == strcmp(name, "flags")) {
    divertmode_update(config_divert_enabled() ? DIVERT_MODE_ECO : DIVERT_MODE_NORMAL);
  }
}

void config_commit()
{
  if(false == modified) {
    return;
  }

  DBUGF("Saving config");
  
  EEPROM.begin(EEPROM_SIZE);

  String jsonStr;
  config_serialize(jsonStr, false, true, false);
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
    for(size_t i = 0; i < opts_length; i++) {
      opts[i]->deserialize(doc);
    }

    config_set<String>("timezone", time_zone);

    return true;
  }

  return false;
}

bool config_serialize(String& json, bool longNames, bool compactOutput, bool hideSecrets)
{
  const size_t capacity = JSON_OBJECT_SIZE(30) + EEPROM_SIZE;
  DynamicJsonDocument doc(capacity);

  config_serialize(doc, longNames, compactOutput, hideSecrets);

  serializeJson(doc, json);

  return true;
}

bool config_serialize(DynamicJsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets)
{
  for(size_t i = 0; i < opts_length; i++) {
    opts[i]->serialize(doc, longNames, compactOutput, hideSecrets);
  }

  return true;
}

void config_set_defaults()
{
  for(size_t i = 0; i < opts_length; i++) {
    opts[i]->setDefault();
  }
}

template <typename T> void config_set(const char *name, T val)
{
  for(size_t i = 0; i < opts_length; i++) {
    if(0 == strcmp(name, opts[i]->name())) {
      opts[i]->set<T>(val);
    }
  }
}

void config_save_emoncms(bool enable, String server, String node, String apikey,
                    String fingerprint)
{
  uint32_t newflags = flags & ~CONFIG_SERVICE_EMONCMS;
  if(enable) {
    newflags |= CONFIG_SERVICE_EMONCMS;
  }

  config_set<String>("emoncms_server", server);
  config_set<String>("emoncms_node", node);
  config_set<String>("emoncms_apikey", apikey);
  config_set<String>("emoncms_fingerprint", fingerprint);
  config_set<uint32_t>("flags", newflags);
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

  config_set<String>("mqtt_server", server);
  config_set<uint32_t>("mqtt_port", port);
  config_set<String>("mqtt_topic", topic);
  config_set<String>("mqtt_user", user);
  config_set<String>("mqtt_pass", pass);
  config_set<String>("mqtt_solar", solar);
  config_set<String>("mqtt_grid_ie", grid_ie);
  config_set<uint32_t>("flags", newflags);
  config_commit();
}

void
config_save_admin(String user, String pass) {
  config_set<String>("www_username", user);
  config_set<String>("www_password", pass);
  config_commit();
}

void
config_save_sntp(bool sntp_enable, String tz) 
{
  uint32_t newflags = flags & ~CONFIG_SERVICE_SNTP;
  if(sntp_enable) {
    newflags |= CONFIG_SERVICE_SNTP;
  }

  config_set<String>("time_zone", tz);
  config_set<uint32_t>("flags", newflags);
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
  config_set<String>("esp_hostname", hostname);
  config_set<String>("sntp_hostname", sntp_host);
  config_commit();
}

void
config_save_wifi(String qsid, String qpass)
{
  config_set<String>("esid", qsid);
  config_set<String>("epass", qpass);
  config_commit();
}

void
config_save_ohm(bool enable, String qohm)
{
  uint32_t newflags = flags & ~CONFIG_SERVICE_OHM;
  if(enable) {
    newflags |= CONFIG_SERVICE_OHM;
  }

  config_set<String>("ohm", qohm);
  config_set<uint32_t>("flags", newflags);
  config_commit();
}

void
config_save_flags(uint32_t newFlags) {
  config_set<uint32_t>("flags", newFlags);
  config_commit();
}

void
config_reset() {
  ResetEEPROM();
  config_set_defaults();
}
