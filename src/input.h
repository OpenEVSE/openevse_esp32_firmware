#ifndef _EMONESP_INPUT_H
#define _EMONESP_INPUT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "RapiSender.h"
#include "evse_man.h"

extern RapiSender &rapiSender;
extern EvseManager evse;

extern String url;
extern String data;

extern long pilot;  // OpenEVSE Pilot Setting

//Defaults OpenEVSE Settings
extern byte rgb_lcd;
extern byte serial_dbg;
extern byte auto_service;
extern int service;

extern long current_scale;
extern long current_offset;

//Default OpenEVSE Safety Configuration
extern byte diode_ck;
extern byte gfci_test;
extern byte ground_ck;
extern byte stuck_relay;
extern byte vent_ck;
extern byte temp_ck;
extern byte auto_start;

extern String firmware;
extern String protocol;

extern String ohm_hour;

extern void handleRapiRead();
extern void create_rapi_json(JsonDocument &data);

extern void input_setup();

#endif // _EMONESP_INPUT_H
