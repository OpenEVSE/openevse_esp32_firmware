// Shelly LNM datagram parser.
//
//   [header (12 bytes)] [payload (payload_len bytes)] [meta (meta_len bytes)]
//
// Binary header (little-endian, packed):
//   offset 0  size 2  magic        0x53 0x4C ("SL")
//   offset 2  size 1  version      protocol version (currently 0)
//   offset 3  size 1  payload_type 1 = status/event
//   offset 4  size 2  payload_len  payload length in bytes (LE)
//   offset 6  size 2  meta_len     meta block length (currently 0)
//   offset 8  size 4  reserved
//
// For payload_type 1 the payload is a JSON object, e.g.
//   {"device":"shellyproem50-...","status":{"em1:0":{"id":0,"voltage":241.8,"current":3.559,"act_power":291.5}}}
//   {"device":"shellypro3em-...","status":{"em:0":{"a_voltage":236.2,"a_act_power":0.0,...,"total_act_power":186.310}}}
//
// On the 3EM the voltage is only reported per phase (a_voltage, ...) while the
// power is reported per phase and as a total (total_act_power).

#include <string.h>
#include <ArduinoJson.h>

#include <cmath>

#include "shelly_lnm_parser.h"

ShellyLnmParseResult shelly_lnm_parse(const uint8_t *data, size_t len,
                                      const char *powerField,
                                      const char *voltageField,
                                      const char *deviceFilter,
                                      ShellyLnmReading &out)
{
  if(len < SHELLY_LNM_HEADER_LEN) {
    return ShellyLnmParseResult::TooShort;
  }

  if(data[0] != 0x53 || data[1] != 0x4C) {
    return ShellyLnmParseResult::BadMagic;
  }

  uint8_t payloadType = data[3];
  uint16_t payloadLen = data[4] | (data[5] << 8);
  uint16_t metaLen = data[6] | (data[7] << 8);

  if(1 != payloadType) {
    return ShellyLnmParseResult::NotStatus;
  }

  if(0 == payloadLen || SHELLY_LNM_HEADER_LEN + (size_t)payloadLen + metaLen > len) {
    return ShellyLnmParseResult::Truncated;
  }

  bool haveVoltageField = voltageField && *voltageField;
  bool haveDeviceFilter = deviceFilter && *deviceFilter;

  // Only keep the device name and the two requested fields so the document
  // stays small whatever the size of the datagram.
  StaticJsonDocument<192> filter;
  filter["device"] = true;
  filter["status"]["*"][powerField] = true;
  if(haveVoltageField) {
    filter["status"]["*"][voltageField] = true;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, data + SHELLY_LNM_HEADER_LEN, payloadLen,
                                             DeserializationOption::Filter(filter));
  if(err) {
    return ShellyLnmParseResult::BadJson;
  }

  if(haveDeviceFilter) {
    const char *device = doc["device"] | "";
    if(0 != strcmp(device, deviceFilter)) {
      return ShellyLnmParseResult::WrongDevice;
    }
  }

  JsonObject status = doc["status"].as<JsonObject>();
  if(status.isNull()) {
    return ShellyLnmParseResult::NoStatus;
  }

  ShellyLnmReading result;
  for(JsonPair kv : status)
  {
    JsonObject component = kv.value().as<JsonObject>();
    if(component.isNull()) {
      continue;
    }

    if(!result.hasPower && component[powerField].is<float>()) {
      float v = component[powerField].as<float>();
      if(std::isfinite(v)) { result.power = v; result.hasPower = true; }
    }

    if(haveVoltageField && !result.hasVoltage && component[voltageField].is<float>()) {
      float v = component[voltageField].as<float>();
      if(std::isfinite(v)) { result.voltage = v; result.hasVoltage = true; }
    }

    if(result.hasPower && (result.hasVoltage || !haveVoltageField)) {
      break;
    }
  }

  if(!result.hasPower && !result.hasVoltage) {
    return ShellyLnmParseResult::NoFields;
  }

  out = result;
  return ShellyLnmParseResult::Ok;
}
