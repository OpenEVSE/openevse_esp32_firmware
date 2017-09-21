#ifndef _EMONESP_INPUT_H
#define _EMONESP_INPUT_H

#include <Arduino.h>
#include "RapiSender.h"

extern RapiSender rapiSender;

extern String url;
extern String data;

extern long amp;    // OpenEVSE Current Sensor
extern long volt;   // Not currently in used
extern long temp1;  // Sensor DS3232 Ambient
extern long temp2;  // Sensor MCP9808 Ambient
extern long temp3;  // Sensor TMP007 Infared
extern long pilot;  // OpenEVSE Pilot Setting
extern long state;    // OpenEVSE State
extern long elapsed;  // Elapsed time (only valid if charging)
extern String estate; // Common name for State

//Defaults OpenEVSE Settings
extern byte rgb_lcd;
extern byte serial_dbg;
extern byte auto_service;
extern int service;

#ifdef ENABLE_LEGACY_API
extern long current_l1min;
extern long current_l2min;
extern long current_l1max;
extern long current_l2max;
#endif

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

//Default OpenEVSE Fault Counters
extern long gfci_count;
extern long nognd_count;
extern long stuck_count;

//OpenEVSE Session
#ifdef ENABLE_LEGACY_API
extern long kwh_limit;
extern long time_limit;
#endif

//OpenEVSE Usage Statistics
extern long wattsec;
extern long watthour_total;

extern String ohm_hour;

extern unsigned long comm_sent;
extern unsigned long comm_success;

extern void handleRapiRead();
extern void update_rapi_values();
extern void create_rapi_json();
extern void on_rapi_event();


#endif // _EMONESP_INPUT_H
