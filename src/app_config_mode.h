#ifndef app_config_mode_h
#define app_config_mode_h

#include <ConfigOpt.h>
#include <ConfigOptDefinition.h>

#include "mqtt.h"
#include "app_config.h"

class ConfigOptVirtualChargeMode : public ConfigOpt
{
protected:
  ConfigOptDefinition<uint32_t> &_base;
  ConfigOptDefinition<uint32_t> &_change;

public:
  ConfigOptVirtualChargeMode(ConfigOptDefinition<uint32_t> &base, ConfigOptDefinition<uint32_t> &change, const char *long_name, const char *short_name) :
    ConfigOpt(long_name, short_name),
    _base(base),
    _change(change)
  {
  }

  String get() {
    int mode = (_base.get() & CONFIG_CHARGE_MODE) >> 10;
    return 0 == mode ? "fast" : "eco";
  }

  virtual bool set(String value) {
    DBUGF("Set charge mode to %s", value.c_str());
    uint32_t newVal = _base.get() & ~CONFIG_CHARGE_MODE;
    if(value == "eco") {
      newVal |= 1 << 10;
    }

    if(_base.set(newVal)) {
      uint32_t new_change = _change.get() | CONFIG_CHARGE_MODE;
      _change.set(new_change);
      return true;
    }

    return false;
  }

  virtual bool serialize(DynamicJsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets) {
    if(!compactOutput) {
      doc[name(longNames)] = get();
      return true;
    }

    return false;
  }

  virtual bool deserialize(DynamicJsonDocument &doc) {
    if(doc.containsKey(_long)) {
      return set(doc[_long].as<String>());
    } else if(doc.containsKey(_short)) { \
      return set(doc[_short].as<String>());
    }

    return false;
  }

  virtual void setDefault() {
  }
};

#endif
