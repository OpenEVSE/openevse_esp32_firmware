#ifndef app_config_mqtt_h
#define app_config_mqtt_h

#include <ConfigOpt.h>
#include <ConfigOptDefenition.h>

#include "mqtt.h"
#include "app_config.h"

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

  virtual bool set(String value) {
    uint32_t newVal = _base.get() & ~CONFIG_MQTT_PROTOCOL;
    if(value == "mqtts") {
      newVal |= 1 << 4;
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
