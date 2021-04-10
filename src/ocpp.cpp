/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#include "ocpp.h"

ArduinoOcppTask::ArduinoOcppTask() : MicroTasks::Task() {

}

void ArduinoOcppTask::setup() {

}

unsigned long ArduinoOcppTask::loop(MicroTasks::WakeReason reason) {

    return MicroTask.Infinate;
}

