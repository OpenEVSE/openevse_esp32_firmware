/*
 * OevseMcp9808 -- thin MCP9808 temperature-sensor wrapper presenting the small
 * subset of the Adafruit_MCP9808 API that evse_monitor.cpp uses
 * (begin/setResolution/wake/readTempC).
 *
 * On every board except the ESP32-P4 this just forwards to Adafruit_MCP9808 over
 * the legacy Arduino `Wire` driver, unchanged.
 *
 * On the P4 (I2C_USE_IDF_MASTER) the sensor shares the GT911 touch bus
 * (CN3 = GPIO7/8, I2C_NUM_1) which is owned by the *new* IDF i2c_master driver;
 * the legacy Wire driver can't share that port, so we talk to the chip directly
 * via the i2c_master device API. The device is added lazily so it still works if
 * evse_monitor::setup() runs before the display task creates the bus.
 */
#ifndef MCP9808_COMPAT_H
#define MCP9808_COMPAT_H

#if defined(ENABLE_MCP9808)

#if defined(I2C_USE_IDF_MASTER)

#include <math.h>
#include "display_p4/i2c_shared.h"

class OevseMcp9808
{
public:
  bool begin()
  {
    ensureDevice();          // best-effort; readTempC() retries if the bus
    return _dev != nullptr;  // wasn't up yet at setup() time
  }

  // 0..3 -> 0.5 / 0.25 / 0.125 / 0.0625 degC (MCP9808 RESOLUTION reg 0x08).
  void setResolution(uint8_t res) { _resolution = res & 0x03; _configured = false; }

  // Clear the shutdown (SHDN) bit so the chip samples continuously.
  void wake() { _configured = false; ensureConfigured(); }

  double readTempC()
  {
    if (!ensureConfigured()) {
      return NAN;
    }
    uint8_t reg = 0x05;      // ambient temperature register
    uint8_t raw[2] = {0, 0};
    if (i2c_master_transmit_receive(_dev, &reg, 1, raw, 2, 100) != ESP_OK) {
      return NAN;
    }
    uint8_t upper = raw[0] & 0x1F;
    double t;
    if (upper & 0x10) {      // sign bit -> negative temperature
      upper &= 0x0F;
      t = 256.0 - ((double)(upper << 4) + (double)raw[1] / 16.0);
    } else {
      t = (double)(upper << 4) + (double)raw[1] / 16.0;
    }
    return t;
  }

private:
  void ensureDevice()
  {
    if (_dev == nullptr) {
      dp4_i2c_add_device(0x18, 100000, &_dev);  // MCP9808 default address
    }
  }

  bool ensureConfigured()
  {
    ensureDevice();
    if (_dev == nullptr) {
      return false;
    }
    if (_configured) {
      return true;
    }
    // RESOLUTION (0x08, 8-bit) then CONFIG (0x01, 16-bit) cleared = wake/run.
    uint8_t res_cmd[2] = {0x08, _resolution};
    uint8_t cfg_cmd[3] = {0x01, 0x00, 0x00};
    bool ok = i2c_master_transmit(_dev, res_cmd, sizeof(res_cmd), 100) == ESP_OK;
    ok = (i2c_master_transmit(_dev, cfg_cmd, sizeof(cfg_cmd), 100) == ESP_OK) && ok;
    _configured = ok;
    return ok;
  }

  i2c_master_dev_handle_t _dev = nullptr;
  uint8_t _resolution = 0;
  bool _configured = false;
};

#else  // !I2C_USE_IDF_MASTER -- legacy Wire + Adafruit library, unchanged

#include <Wire.h>
#include <Adafruit_MCP9808.h>

class OevseMcp9808
{
public:
  bool begin() { return _mcp.begin(); }
  void setResolution(uint8_t res) { _mcp.setResolution(res); }
  void wake() { _mcp.wake(); }
  double readTempC() { return _mcp.readTempC(); }

private:
  Adafruit_MCP9808 _mcp;
};

#endif // I2C_USE_IDF_MASTER

#endif // ENABLE_MCP9808
#endif // MCP9808_COMPAT_H
