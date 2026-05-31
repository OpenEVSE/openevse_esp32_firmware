# P4 RFID/Temp on the Shared CN3 I2C Bus

**Goal:** Run the PN532 RFID reader (and MCP9808 temp sensor) on the ESP32-P4's CN3
connector, which is the GT911 touch/codec I2C bus — without disturbing the working
display.

## Background / root constraint

- CN3 (silkscreen) = `ES_I2C_SDA`/`ES_I2C_SCL` = **GPIO7 (SDA) / GPIO8 (SCL)** =
  `I2C_NUM_1`. Confirmed from `JC4880P443_V1.0.pdf` (CN3 is a 6-pin header: 1 GND,
  2 ESP_3V3, 3 SCL, 4 SDA, 5/6 GND). It is the *only* I2C bus broken out to a
  connector; GPIO32/33 are plain GPIOs on the 2×13 header.
- The GT911 touch uses the **new** ESP-IDF `i2c_master` driver
  (`i2c_new_master_bus` in `display_p4.cpp`, `esp_lcd_new_panel_io_i2c`).
- Arduino `Wire`/`Wire1` in the pinned core-3 framework uses the **legacy** IDF
  driver (`i2c_driver_install`, see `framework-arduinoespressif32/cores/esp32/esp32-hal-i2c.c:142`).
- **Legacy and new I2C drivers cannot share a port.** So PN532 + MCP9808 (Wire-based)
  cannot ride GPIO7/8 via Wire. They must use the new-driver device API and attach
  to the touch's bus handle.

## Design (P4-only; all other boards unchanged)

- New `src/display_p4/i2c_shared.{h,cpp}`: `dp4_i2c_add_device(addr, scl_hz, &dev)`
  fetches the port-1 bus via `i2c_master_get_bus_handle(1, …)` and registers a
  device. Returns false (caller retries) if the display task hasn't created the bus
  yet. Gated by `I2C_USE_IDF_MASTER`.
- `pn532.cpp`: I/O isolated into `pn532_send()` / `pn532_recv()`. Under
  `I2C_USE_IDF_MASTER` these use `i2c_master_transmit`/`i2c_master_receive` against a
  lazily-added device (0x24); otherwise the original Wire calls, byte-for-byte.
  `begin()` skips `Wire.begin()` on P4.
- `src/mcp9808_compat.h`: `OevseMcp9808` exposes the 4 methods evse_monitor uses
  (`begin/setResolution/wake/readTempC`). Non-P4 → forwards to `Adafruit_MCP9808`
  over Wire. P4 → direct new-driver reader (ambient reg 0x05, resolution 0x08,
  config 0x01), device 0x18 added lazily so it survives setup ordering.
- `evse_monitor.{h,cpp}`: type `Adafruit_MCP9808 _mcp9808` → `OevseMcp9808`;
  `Wire.begin()` guarded out on P4.
- `platformio.ini [env:openevse_p4]`: `I2C_SDA=7 / I2C_SCL=8`, add
  `-D I2C_USE_IDF_MASTER=1`.

## Hardware validation (task #23)

1. Flash; on serial the PN532 should reach "connection to PN532 active". Present a
   tag → UID logged + RFID event. If absent: "RFID chip not found" after 60 s.
2. Confirm the **touch still works** (regression check — bus creation untouched, so
   it should).
3. MCP9808: if wired to CN3, `readTempC()` returns a sane ambient temp; if not
   present it just reads NaN (harmless), same as before.
