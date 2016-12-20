#ifndef _EMONESP_EMONCMS_H
#define _EMONESP_EMONCMS_H

#include <Arduino.h>


extern boolean emoncms_connected;
extern unsigned long packets_sent;
extern unsigned long packets_success;


void emoncms_publish(String data);

#endif // _EMONESP_EMONCMS_H
