// Solar PV power diversion
// Modulate charge rate based on solar PV output
// Glyn Hudson | OpenEnergyMonitor.org

#ifndef _EMONESP_DIVERT_H
#define _EMONESP_DIVERT_H

#include <Arduino.h>

// global variable
extern byte divertmode;
extern int solar;
extern int grid_ie;

// Change mode
void divertmode_update(byte divertmode);

// Set charge rate depending on charge mode and solarPV output
void divert_current_loop();

#endif // _EMONESP_DIVERT_H
