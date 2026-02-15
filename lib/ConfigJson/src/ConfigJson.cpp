#include <EEPROM.h>
#include <MicroDebug.h>

#include "ConfigJson.h"

const char ConfigOptSecret::_DUMMY_PASSWORD[] PROGMEM = "_DUMMY_PASSWORD";
const __FlashStringHelper * ConfigOptSecret::DUMMY_PASSWORD = FPSTR(_DUMMY_PASSWORD);

ConfigJson::ConfigJson(ConfigOpt **opts, size_t len, size_t storageSize, size_t storageOffset) :
  _opts(opts),
  _len(len),
  _storage_size(storageSize),
  _storage_offset(storageOffset),
  _change(nullptr)
{
}

bool ConfigJson::load(bool merge) 
{
  bool loaded = false;

  if(false == merge) {
    reset();
  }

  EEPROM.begin(_storage_size);

  char start = 0;
  uint8_t a = 0, b = 0;
  EEPROM.get(_storage_offset + 0, a);
  EEPROM.get(_storage_offset + 1, b);
  size_t length = a | (b << 8);

  EEPROM.get(_storage_offset + 2, start);

  DBUGF("Got %d %c from EEPROM @ 0x%04x", length, start, _storage_offset);

  if(2 <= length && length < _storage_size &&
    '{' == start)
  {
    char json[length + 1];
    for(size_t i = 0; i < length; i++) {
      json[i] = EEPROM.read(_storage_offset + 2 + i);
    }
    json[length] = '\0';
    DBUGF("Found stored JSON %s", json);
    deserialize(json);
    _modified = false;
    loaded = true;
  } else {
    DBUGF("No JSON config found");
  }

  EEPROM.end();

  return loaded;
}


void ConfigJson::commit()
{
  if(false == _modified) {
    return;
  }

  DBUGF("Saving config");
  
  EEPROM.begin(_storage_size);

  String jsonStr;
  ConfigJson::serialize(jsonStr, false, true, false);
  const char *json = jsonStr.c_str();
  DBUGF("Writing %s to EEPROM", json);
  int length = jsonStr.length();
  EEPROM.put(_storage_offset + 0, length & 0xff);
  EEPROM.put(_storage_offset + 1, (length >> 8) & 0xff);
  for(int i = 0; i < length; i++) {
    EEPROM.write(_storage_offset + 2 + i, json[i]);
  }

  DBUGF("%d bytes written to EEPROM @ 0x%04x, committing", length + 2, _storage_offset);

  if(EEPROM.commit())
  {
    DBUGF("Done");
    _modified = false;
  } else {
    DBUGF("Writting EEPROM failed");
  }

  EEPROM.end();
}

bool ConfigJson::deserialize(const char *json) 
{
  JsonDocument doc;
  
  DeserializationError err = deserializeJson(doc, json);
  if(DeserializationError::Code::Ok == err)
  {
    ConfigJson::deserialize(doc);

    return true;
  }

  return false;
}

bool ConfigJson::deserialize(JsonDocument &doc) 
{
  bool changed = false;

  for(size_t i = 0; i < _len; i++) {
    if(_opts[i]->deserialize(doc))
    {
      _modified = true;
      changed = true;
      if(_change) {
        _change(_opts[i]->name());
      }
    }
  }

  return changed;
}

bool ConfigJson::serialize(String& json, bool longNames, bool compactOutput, bool hideSecrets)
{
  JsonDocument doc;

  if(ConfigJson::serialize(doc, longNames, compactOutput, hideSecrets))
  {
    serializeJson(doc, json);
    return true;
  }

  return false;
}

bool ConfigJson::serialize(Print& json, bool longNames, bool compactOutput, bool hideSecrets)
{
  JsonDocument doc;

  if(ConfigJson::serialize(doc, longNames, compactOutput, hideSecrets))
  {
    serializeJson(doc, json);
    return true;
  }

  return false;
}

bool ConfigJson::serialize(JsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets)
{
  for(size_t i = 0; i < _len; i++) {
    _opts[i]->serialize(doc, longNames, compactOutput, hideSecrets);
  }

  return true;
}

void ConfigJson::reset()
{
  for(size_t i = 0; i < _len; i++) {
    _opts[i]->setDefault();
  }
  _modified = true;
}

/*
template <typename T> bool ConfigJson::set(const char *name, T val)
{
  DBUG("Attempt set ");
  DBUG(name);
  DBUG(" to ");
  DBUGLN(val);

  const size_t capacity = JSON_OBJECT_SIZE(1) + 256;
  DynamicJsonDocument doc(capacity);
  doc[name] = val;
  return ConfigJson::deserialize(doc);
}
*/
