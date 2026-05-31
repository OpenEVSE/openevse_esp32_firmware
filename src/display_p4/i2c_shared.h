/*
 * Shared IDF i2c_master bus access for the ESP32-P4 board.
 *
 * On the Guition JC4880P443C the only I2C bus broken out to connectors (CN3 =
 * ES_I2C) is GPIO7/8 on I2C_NUM_1 -- the same bus the GT911 touch + ES8311 codec
 * use via the *new* ESP-IDF i2c_master driver. The display task
 * (display_p4.cpp) creates that bus with i2c_new_master_bus(); this helper lets
 * the PN532 RFID (0x24) and MCP9808 temp (0x18) drivers attach to it as devices.
 *
 * Arduino `Wire` here is the LEGACY i2c driver, which cannot coexist with the new
 * driver on the same port -- hence those drivers must use this device API instead
 * of Wire on the P4. Gated by I2C_USE_IDF_MASTER (set only for env:openevse_p4).
 */
#ifndef DISPLAY_P4_I2C_SHARED_H
#define DISPLAY_P4_I2C_SHARED_H

#if defined(I2C_USE_IDF_MASTER)

#include <stdint.h>
#include <stddef.h>
#include "driver/i2c_master.h"

// Fetch the shared port-1 i2c_master bus created by the display task and register
// a device at `addr` (7-bit) running at `scl_hz`. Returns true and fills *out on
// success. Returns false if the bus has not been created yet (caller should
// retry later) or the device could not be added.
bool dp4_i2c_add_device(uint8_t addr, uint32_t scl_hz, i2c_master_dev_handle_t *out);

#endif // I2C_USE_IDF_MASTER
#endif // DISPLAY_P4_I2C_SHARED_H
