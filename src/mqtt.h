#ifndef _EMONESP_MQTT_H
#define _EMONESP_MQTT_H

// -------------------------------------------------------------------
// MQTT support
// -------------------------------------------------------------------

#include <Arduino.h>
#include <ArduinoJson.h>
#include "evse_man.h"
#include "limit.h"

enum mqtt_protocol {
	MQTT_PROTOCOL_MQTT,
	MQTT_PROTOCOL_MQTT_SSL,
	MQTT_PROTOCOL_WEBSOCKET,
	MQTT_PROTOCOL_WEBSOCKET_SSL
};

#define MQTT_LOOP	500

extern void mqtt_msg_callback();

// -------------------------------------------------------------------
// Perform the background MQTT operations. Must be called in the main
// loop function
// -------------------------------------------------------------------
extern void mqtt_loop();

// -------------------------------------------------------------------
// Publish values to MQTT
//
// data: a comma seperated list of name:value pairs to send
// -------------------------------------------------------------------
extern void mqtt_publish(JsonDocument &data);
extern void mqtt_publish_claim();
extern void mqtt_set_claim(bool override, EvseProperties &props);
extern void mqtt_publish_override();
extern void mqtt_publish_json(JsonDocument &data, const char* topic);
extern void mqtt_publish_schedule();
extern void mqtt_set_schedule(String schedule);
extern void mqtt_clear_schedule(uint32_t event);
extern void mqtt_publish_limit();
extern void mqtt_set_limit(LimitProperties &limitProps);

// -------------------------------------------------------------------
// Restart the MQTT connection
// -------------------------------------------------------------------
extern void mqtt_restart();


// -------------------------------------------------------------------
// Return true if we are connected to an MQTT broker, false if not
// -------------------------------------------------------------------
extern boolean mqtt_connected();

#endif // _EMONESP_MQTT_H
