#ifndef _ConfigJson_h
#define _ConfigJson_h

#include <functional>

#include "ConfigOpt.h"
#include "ConfigOptDefinition.h"
#include "ConfigOptSecret.h"
#include "ConfigOptVirtualBool.h"
#include "ConfigOptVirtualMaskedBool.h"

typedef std::function<void(String name)> ConfigJsonChangeHandler;

class ConfigJson
{
private:
  bool _modified;

  ConfigOpt **_opts;
  size_t _len;
  size_t _storage_size;
  size_t _storage_offset;

  ConfigJsonChangeHandler _change;
public:
  ConfigJson(ConfigOpt **opts, size_t len, size_t storageSize, size_t storageOffset = 0);

  void reset();
  void commit();
  bool load(bool merge = false);

  bool serialize(String& json, bool longNames = true, bool compactOutput = false, bool hideSecrets = false);
  bool serialize(Print& json, bool longNames = true, bool compactOutput = false, bool hideSecrets = false);
  bool serialize(JsonDocument &doc, bool longNames = true, bool compactOutput = false, bool hideSecrets = false);

  bool deserialize(String& json) {
    return deserialize(json.c_str());
  }
  bool deserialize(const char *json);
  bool deserialize(JsonDocument &doc);

  template <typename T> bool set(const char *name, T val)
  {
    JsonDocument doc;
    doc[name] = val;
    return ConfigJson::deserialize(doc);
  }

  bool set(const char *name, uint32_t val) {
    return set<uint32_t>(name, val);
  } 
  bool set(const char *name, int val) {
    return set<int>(name, val);
  } 
  bool set(const char *name, String &val) {
    return set<String>(name, val);
  } 
  bool set(const char *name, bool val) {
    return set<bool>(name, val);
  } 
  bool set(const char *name, double val) {
    return set<double>(name, val);
  } 
  bool set(String &name, uint32_t val) {
    return set<uint32_t>(name.c_str(), val);
  } 
  bool set(String &name, int val) {
    return set<int>(name.c_str(), val);
  } 
  bool set(String &name, String &val) {
    return set<String>(name.c_str(), val);
  } 
  bool set(String &name, bool val) {
    return set<bool>(name.c_str(), val);
  } 
  bool set(String &name, double val) {
    return set<double>(name.c_str(), val);
  } 

  void onChanged(ConfigJsonChangeHandler handler) {
    _change = handler;
  }
};

#endif // _ConfigJson_h
