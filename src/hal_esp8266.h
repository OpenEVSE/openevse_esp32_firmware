#ifndef _HAL_H
#error Do not include directly, #include "hal.h" 
#endif // !_HAL_H

class HalEsp8622 : public HalClass
{
  public:
    virtual uint64_t getChipId() {
      return ESP.getChipId();
    }
    virtual uint32_t getFreeHeap() {
      return ESP.getFreeHeap();
    }
    virtual uint32_t getFlashChipSize() {
      return ESP.getFlashChipSize();
    }
    virtual void reset() {
      ESP.reset();
    }
    virtual void eraseConfig() {
      ESP.eraseConfig();
    }

};
