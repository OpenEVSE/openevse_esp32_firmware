// Shelly Local Network Messaging (LNM) local power / voltage source.
//
// Joins the configured UDP multicast group and keeps the most recent grid
// active power and voltage values reported by Shelly devices (energy meters,
// plugs, ...) using their LNM status messages. When enabled the values update
// the divert grid_ie / voltage used by the divert task and the current
// shaper, replacing the MQTT feeds.

#include <ArduinoJson.h>

#include "emonesp.h"
#include "shelly_lnm.h"
#include "shelly_lnm_parser.h"
#include "net_manager.h"
#include "divert.h"
#include "current_shaper.h"
#include "event.h"
#include "debug.h"

ShellyLnmTask shelly_lnm; // global instance

ShellyLnmTask::ShellyLnmTask() :
  MicroTasks::Task(),
  _listening(false),
  _listenPort(0),
  _power(NAN),
  _voltage(NAN),
  _lastUpdate(0),
  _buffer(nullptr),
  _pendingPower(false),
  _pendingVoltage(false),
  _lastApply(0),
  _appliedVoltage(NAN)
{
  memset(_listenAddr, 0, sizeof(_listenAddr));
}

void ShellyLnmTask::begin()
{
  MicroTask.startTask(this);
}

void ShellyLnmTask::setup()
{
  DBUGLN("Shelly LNM setup");
}

unsigned long ShellyLnmTask::loop(MicroTasks::WakeReason reason)
{
  if(!shelly_lnm_enabled) {
    if(_listening) {
      stop();
    }
    return MicroTask.Infinate;
  }

  if(!net.isConnected()) {
    if(_listening) {
      stop();
    }
    return SHELLY_LNM_LOOP_TIME;
  }

  // Retry failed starts and apply listener configuration changes.
  if(isConfigChanged()) {
    DBUGLN("Shelly LNM config changed, restarting listener");
    stop();
  }
  if(!_listening) {
    start();
  }

  // Back off while the listener cannot start (e.g. a bad address) rather than
  // retrying and logging at the packet polling rate
  if(!_listening) {
    return SHELLY_LNM_RETRY_TIME;
  }

  // Process all queued packets, keep only the most recent values
  int packetSize;
  while((packetSize = _udp.parsePacket()) > 0)
  {
    if(packetSize > SHELLY_LNM_BUFFER_SIZE) {
      _udp.flush();
      DBUGF("Shelly LNM packet too large: %d", packetSize);
      continue;
    }

    int len = _udp.read(_buffer, SHELLY_LNM_BUFFER_SIZE);
    if(len > 0) {
      processPacket(_buffer, len);
    }
  }

  // Shelly devices report about once per second. Applying every packet would
  // flood divert, the shaper, events, emoncms and the RAPI queue, and the
  // smoothing filters work with whole seconds, so apply at a slower pace.
  if((_pendingPower || _pendingVoltage) &&
     (0 == _lastApply || (long)(millis() - _lastApply) >= (long)SHELLY_LNM_APPLY_INTERVAL))
  {
    applyPending();
  }

  return SHELLY_LNM_LOOP_TIME;
}

bool ShellyLnmTask::isConfigChanged()
{
  // Measurement fields are read on each packet; only the endpoint requires
  // restarting the listener.
  return _listening &&
         (shelly_lnm_addr != _listenAddr ||
          shelly_lnm_port != _listenPort);
}

void ShellyLnmTask::start()
{
  if(!shelly_lnm_enabled) {
    return;
  }

  if(!net.isConnected()) {
    return;
  }

  // Idempotent: loop() calls start() whenever not listening, which also
  // happens while the network is down. Re-joining the same multicast group
  // would be a silent but useless operation.
  if(_listening && shelly_lnm_addr == _listenAddr && shelly_lnm_port == _listenPort) {
    return;
  }

  stop();

  IPAddress addr;
  if(!addr.fromString(shelly_lnm_addr.c_str())) {
    DBUGF("Invalid Shelly LNM multicast address: %s", shelly_lnm_addr.c_str());
    return;
  }
  if((addr[0] & 0xF0) != 0xE0) {
    DBUGF("Shelly LNM address %s is not a multicast address (224.0.0.0 - 239.255.255.255)", shelly_lnm_addr.c_str());
    return;
  }

  if(nullptr == _buffer) {
    _buffer = (uint8_t *)malloc(SHELLY_LNM_BUFFER_SIZE);
    if(nullptr == _buffer) {
      DBUGLN("Shelly LNM: out of memory for the packet buffer");
      return;
    }
  }

#if defined(EPOXY_DUINO)
  bool started = false; // the host mock has no multicast support
#else
  bool started = _udp.beginMulticast(addr, shelly_lnm_port);
#endif
  if(!started) {
    DBUGF("Failed to start Shelly LNM listener on %s:%u", shelly_lnm_addr.c_str(), shelly_lnm_port);
    return;
  }

  strlcpy(_listenAddr, shelly_lnm_addr.c_str(), sizeof(_listenAddr));
  _listenPort = shelly_lnm_port;
  _listening = true;

  DBUGF("Shelly LNM listener started on %s:%u", _listenAddr, _listenPort);
}

void ShellyLnmTask::stop()
{
  if(_listening) {
    _udp.stop();
    _listening = false;
    DBUGLN("Shelly LNM listener stopped");
  }
  // Do not let a quick disable/re-enable reuse a measurement from the
  // previous listener session. The new session must receive a fresh packet.
  _lastUpdate = 0;
  _power = NAN;
  _voltage = NAN;
  _pendingPower = false;
  _pendingVoltage = false;
  _lastApply = 0;
  _appliedVoltage = NAN;
  if(_buffer) {
    free(_buffer);
    _buffer = nullptr;
  }
}

bool ShellyLnmTask::isListening()
{
  return _listening;
}

void ShellyLnmTask::notifyConfigChanged()
{
  // Wake the task up so the new settings apply without waiting for the
  // next poll
  MicroTask.wakeTask(this);
}

// Parse one datagram (see shelly_lnm_parser.cpp) and store the latest values.
// The values are pushed to divert / shaper / EVSE by applyPending().
void ShellyLnmTask::processPacket(const uint8_t *data, size_t len)
{
  ShellyLnmReading reading;
  ShellyLnmParseResult res = shelly_lnm_parse(data, len,
                                              shelly_lnm_power_field.c_str(),
                                              shelly_lnm_voltage_field.c_str(),
                                              shelly_lnm_device.c_str(),
                                              reading);
  if(ShellyLnmParseResult::Ok != res) {
    DBUGF("Shelly LNM: packet ignored (%d)", (int)res);
    return;
  }

  if(reading.hasPower && !isnan(reading.power)) {
    _power = reading.power;
    _lastUpdate = millis();
    _pendingPower = true;
  }

  if(reading.hasVoltage && !isnan(reading.voltage)) {
    _voltage = reading.voltage;
    _pendingVoltage = true;
  }

  DBUGF("Shelly LNM: power=%.1fW voltage=%.1fV", _power, _voltage);
}

void ShellyLnmTask::applyPending()
{
  _lastApply = millis();
  if(0 == _lastApply) {
    _lastApply = 1;
  }

  // Update the global voltage, as the mqtt_vrms feed would do. Ignore sub-volt
  // jitter: every change is forwarded to the EVSE module over RAPI.
  if(_pendingVoltage && (isnan(_appliedVoltage) || fabs(_voltage - _appliedVoltage) >= SHELLY_LNM_VOLTAGE_STEP)) {
    _appliedVoltage = _voltage;
    evse.setMqttVoltage(_voltage);
  }
  _pendingVoltage = false;

  if(_pendingPower) {
    // Update the global grid_ie, as the mqtt_grid_ie feed would do (grid mode only)
    if(DIVERT_TYPE_GRID == divert_type) {
      divert.setGridIe((int)_power);
    }

    // Feed the shaper unless a dedicated live power topic is configured
    if(shaper.getState() && (mqtt_live_pwr == "" || mqtt_live_pwr == mqtt_grid_ie)) {
      shaper.setLivePwr((int)_power);
    }

    if(DIVERT_TYPE_GRID == divert_type) {
      divert.update_state();
    }
  }
  _pendingPower = false;

  JsonDocument doc;
  doc["shelly_lnm_listening"] = _listening ? 1 : 0;
  doc["shelly_lnm_data_age"] = getDataAge();
  doc["shelly_lnm_power"] = _power;
  doc["shelly_lnm_voltage"] = _voltage;
  event_send(doc);
}
