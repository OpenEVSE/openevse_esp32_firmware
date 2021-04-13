/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#include "ocpp.h"

#include "debug.h"

#include <ArduinoOcpp.h> // Facade for ArduinoOcpp

ArduinoOcppTask::ArduinoOcppTask() : MicroTasks::Task() {

}

void ArduinoOcppTask::begin(String CS_hostname, uint16_t CS_port, String CS_url) {
    Serial.println("[ArduinoOcppTask] begin!");
    OCPP_initialize(CS_hostname, CS_port, CS_url);

    MicroTask.startTask(this);
}

void ArduinoOcppTask::setup() {

}

unsigned long ArduinoOcppTask::loop(MicroTasks::WakeReason reason) {
    DEBUG_PORT.println("[ArduinoOcppTask] loop!");
    OCPP_loop();
    return MicroTask.Infinate;
}

