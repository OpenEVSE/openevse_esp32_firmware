#ifndef _ConfigOptVirtualMaskedBool_h
#define _ConfigOptVirtualMaskedBool_h

#include "ConfigOpt.h"
#include "ConfigOptVirtualBool.h"

class ConfigOptVirtualMaskedBool : public ConfigOptVirtualBool
{
protected:
  ConfigOptDefinition<uint32_t> &_change;

public:
  ConfigOptVirtualMaskedBool(ConfigOptDefinition<uint32_t> &base, ConfigOptDefinition<uint32_t> &change, uint32_t mask, uint32_t expected, const char *long_name, const char *short_name) :
    ConfigOptVirtualBool(base, mask, expected, long_name, short_name),
    _change(change)
  {
  }

  virtual bool set(bool value)
  {
    if(!ConfigOptVirtualBool::set(value)) {
      return false;
    }

    uint32_t new_change = _change.get() | _mask;
    _change.set(new_change);

    return true;
  }
};

#endif // _ConfigOptVirtualMaskedBool_h
