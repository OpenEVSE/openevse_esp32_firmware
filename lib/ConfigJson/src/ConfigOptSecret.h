#ifndef _ConfigOptSecret_h
#define _ConfigOptSecret_h

#include "ConfigOptDefinition.h"


class ConfigOptSecret : public ConfigOptDefinition<String>
{
private:
  static const char _DUMMY_PASSWORD[];

public:
  static const __FlashStringHelper * DUMMY_PASSWORD;

  ConfigOptSecret(String &v, String d, const char *l, const char *s) :
    ConfigOptDefinition<String>(v, d, l, s)
  {
  }

  bool set(String &value) {
    if(value.equals(DUMMY_PASSWORD)) {
      return false;
    }

    return ConfigOptDefinition<String>::set(value);
  }

  virtual bool serialize(JsonDocument &doc, bool longNames, bool compactOutput, bool hideSecrets) {
    if(!compactOutput || _val != _default) {
      if(hideSecrets) {
        doc[name(longNames)] = (_val != 0) ? String(DUMMY_PASSWORD) : "";
        return true;
      } else {
        doc[name(longNames)] = _val;
        return true;
      }
    }
    return false;
  }
};

#endif // _ConfigOptSecret_h
