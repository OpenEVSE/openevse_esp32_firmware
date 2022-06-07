#ifndef _EMONESP_MQTT_H
#define _EMONESP_MQTT_H

// -------------------------------------------------------------------
// MQTT support
// -------------------------------------------------------------------

#include <Arduino.h>
#include <ArduinoJson.h>

#define MQTT_PROTOCOL_MQTT          0
#define MQTT_PROTOCOL_MQTT_SSL      1
#define MQTT_PROTOCOL_WEBSOCKET     2
#define MQTT_PROTOCOL_WEBSOCKET_SSL 3

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
extern void mqtt_publish_override();
extern void mqtt_publish_json(JsonDocument &data, const char* topic);
// -------------------------------------------------------------------
// Restart the MQTT connection
// -------------------------------------------------------------------
extern void mqtt_restart();


// -------------------------------------------------------------------
// Return true if we are connected to an MQTT broker, false if not
// -------------------------------------------------------------------
extern boolean mqtt_connected();

#endif // _EMONESP_MQTT_H
