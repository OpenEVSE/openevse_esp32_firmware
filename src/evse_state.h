
#ifndef _OPENEVSE_EVSE_STATE_H
#define _OPENEVSE_EVSE_STATE_H

#include <Arduino.h>

class EvseState
{
  public:
    enum Value : uint8_t {
      None,
      Active,
      Disabled
    };

  EvseState() = default;
  constexpr EvseState(Value value) : _value(value) { }

  bool fromString(const char *value)
  {
    // Cheat a bit and just check the first char
    if('a' == value[0] || 'd' == value[0]) {
      _value = 'a' == value[0] ? EvseState::Active : EvseState::Disabled;
      return true;
    }
    return false;
  }

  const char *toString()
  {
    return EvseState::Active == _value ? "active" :
           EvseState::Disabled == _value ? "disabled" :
           EvseState::None == _value ? "none" :
           "unknown";
  }

  operator Value() const { return _value; }
  explicit operator bool() = delete;        // Prevent usage: if(state)
  EvseState operator= (const Value val) {
    _value = val;
    return *this;
  }

  private:
    Value _value;
};

#endif // _OPENEVSE_EVSE_STATE_H
