#ifndef app_config_mqtt_h
#define app_config_mqtt_h

#include <ConfigOpt.h>
#include <ConfigOptDefinition.h>

#include "mqtt.h"
#include "app_config.h"

class ConfigOptVirtualMqttProtocol : public ConfigOpt
{
protected:
  ConfigOptDefinition<uint32_t> &_base;
  ConfigOptDefinition<uint32_t> &_change;

public:
  ConfigOptVirtualMqttProtocol(ConfigOptDefinition<uint32_t> &base, ConfigOptDefinition<uint32_t> &change, const char *long_name, const char *short_name) :
    ConfigOpt(long_name, short_name),
    _base(base),
    _change(change)
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

    if(_base.set(newVal)) {
      uint32_t new_change = _change.get() | CONFIG_MQTT_PROTOCOL;
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
