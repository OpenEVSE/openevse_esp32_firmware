#ifndef _EMONESP_EMONCMS_H
#define _EMONESP_EMONCMS_H

#include <Arduino.h>

// -------------------------------------------------------------------
// Commutication with EmonCMS
// -------------------------------------------------------------------

extern boolean emoncms_connected;
extern unsigned long packets_sent;
extern unsigned long packets_success;

// -------------------------------------------------------------------
// Publish values to EmonCMS
//
// data: a comma seperated list of name:value pairs to send
// -------------------------------------------------------------------
void emoncms_publish(String data);

#endif // _EMONESP_EMONCMS_H

