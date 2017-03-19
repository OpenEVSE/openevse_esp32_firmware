// Solar PV power diversion
// Modulate charge rate based on solar PV output
// Glyn Hudson | OpenEnergyMonitor.org

#ifndef _EMONESP_DIVERT_H
#define _EMONESP_DIVERT_H

#include <Arduino.h>

// global variable
extern byte divertmode;
extern int solar;
extern int use;

// Change mode
void change_divertmode(byte divertmode);

// Set charge rate depending on charge mode and solarPV output
void solarpv_divert_update();

#endif // _EMONESP_DIVERT_H
