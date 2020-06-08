#ifndef __EVENT_H
#define __EVENT_H

#include <Arduino.h>
#include <ArduinoJson.h>

void event_send(String &event);
void event_send(JsonDocument &event);

#endif
