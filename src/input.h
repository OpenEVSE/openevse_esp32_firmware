#ifndef _EMONESP_INPUT_H
#define _EMONESP_INPUT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "RapiSender.h"
#include "event_log.h"
#include "evse_man.h"

extern RapiSender &rapiSender;
extern EvseManager evse;
extern EventLog eventLog;

extern String url;
extern String data;
extern String ohm_hour;

extern String getOverride();
extern void handleRapiRead();
extern void create_rapi_json(JsonDocument &data);

extern void input_setup();

#endif // _EMONESP_INPUT_H
