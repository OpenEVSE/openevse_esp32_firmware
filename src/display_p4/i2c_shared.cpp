#include "i2c_shared.h"

#if defined(I2C_USE_IDF_MASTER)

#include "hal/i2c_types.h"

// Port the display task creates the shared bus on (matches I2C_NUM_1 in
// display_p4.cpp). The touch + codec live here too.
#define DP4_I2C_PORT ((i2c_port_t)1)

bool dp4_i2c_add_device(uint8_t addr, uint32_t scl_hz, i2c_master_dev_handle_t *out)
{
  if (out == nullptr) {
    return false;
  }

  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_master_get_bus_handle(DP4_I2C_PORT, &bus) != ESP_OK || bus == nullptr) {
    // Display task has not created the bus yet -- caller retries later.
    return false;
  }

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = addr;
  dev_cfg.scl_speed_hz = scl_hz;

  return i2c_master_bus_add_device(bus, &dev_cfg, out) == ESP_OK;
}

#endif // I2C_USE_IDF_MASTER
