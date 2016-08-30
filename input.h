#ifndef _EMONESP_INPUT_H
#define _EMONESP_INPUT_H

#include <Arduino.h>

extern String url;
extern String data;

extern int espvcc;
extern int espflash;
extern int espfree;

extern int commDelay;

extern int amp; //OpenEVSE Current Sensor
extern int volt; //Not currently in used
extern int temp1; //Sensor DS3232 Ambient
extern int temp2; //Sensor MCP9808 Ambient
extern int temp3; //Sensor TMP007 Infared
extern int pilot; //OpenEVSE Pilot Setting
extern long state; //OpenEVSE State
extern String estate; // Common name for State

//Defaults OpenEVSE Settings
extern byte rgb_lcd;
extern byte serial_dbg;
extern byte auto_service;
extern int service;
extern int current_l1;
extern int current_l2;
extern String current_l1min;
extern String current_l2min;
extern String current_l1max;
extern String current_l2max;
extern String current_scale;
extern String current_offset;

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
extern String gfci_count;
extern String nognd_count;
extern String stuck_count;

//OpenEVSE Session options
extern String kwh_limit;
extern String time_limit;

//OpenEVSE Usage Statistics
extern String wattsec;
extern String watthour_total;

extern String ohm_hour;

extern unsigned long comm_sent;
extern unsigned long comm_success;

extern void handleRapiRead();
extern void update_rapi_values();
extern void create_rapi_json();


#endif // _EMONESP_INPUT_H
