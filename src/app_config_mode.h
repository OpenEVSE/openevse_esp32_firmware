#ifndef app_config_mode_h
#define app_config_mode_h

#include <ConfigOpt.h>
#include <ConfigOptDefenition.h>

#include "mqtt.h"
#include "app_config.h"

class ConfigOptVirtualChargeMode : public ConfigOpt
{
protected:
  ConfigOptDefenition<uint32_t> &_base;

public:
  ConfigOptVirtualChargeMode(ConfigOptDefenition<uint32_t> &b, const char *l, const char *s) :
    ConfigOpt(l, s),
    _base(b)
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
    return _base.set(newVal);
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
