#ifndef _EMONESP_MQTT_H
#define _EMONESP_MQTT_H

#include <Arduino.h>

extern void mqtt_loop();
extern void mqtt_publish(String data);
extern void mqtt_restart();
extern boolean mqtt_connected();

#endif // _EMONESP_MQTT_H
