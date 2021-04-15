/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#include "ocpp.h"

#include "debug.h"

#include <ArduinoOcpp.h> // Facade for ArduinoOcpp
#include <ArduinoOcpp/SimpleOcppOperationFactory.h> // define behavior for incoming req messages

#include <ArduinoOcpp/Core/OcppEngine.h> //only for outputting debug messages to SteVe
#include <ArduinoOcpp/MessagesV16/DataTransfer.h> //only for outputting debug messages to SteVe

ArduinoOcppTask::ArduinoOcppTask() : MicroTasks::Task(), bootReadyCallback(MicroTasksCallback([](){})) {

}

void ArduinoOcppTask::begin(String CS_hostname, uint16_t CS_port, String CS_url, EvseManager &evse) {
    Serial.println("[ArduinoOcppTask] begin!");
    OCPP_initialize(CS_hostname, CS_port, CS_url);

    this->evse = &evse;

    MicroTask.startTask(this);
}

void ArduinoOcppTask::setup() {
/*    bootReadyCallback = MicroTasksCallback([evse = evse] () {
        Serial.print("[BootReadyCallback] Listener. Ignore until run on real EVSE\n");
//      bootNotification(evse->getFirmwareVersion(), "OpenEVSE", [](JsonObject payload) {
//        Serial.print("[main] BootNotification successful!\n");
//      });
    });

    evse->onBootReady((MicroTasks::EventListener*) &bootReadyCallback);*/

    bootNotification("Advanced Series", "OpenEVSE", [](JsonObject payload) { //alternative to listener approach above for development
        Serial.print("[ArduinoOcppTask] BootNotification initiated manually. Remove when run on real EVSE\n");
    });

    setPowerActiveImportSampler([evse = evse]() {
        return (float) (evse->getAmps() * evse->getVoltage());
    });

    setEnergyActiveImportSampler([evse = evse] () {
        return (float) evse->getTotalEnergy();
    });

    //setEvRequestsEnergySampler([ &evse = evse] () {
    //    return (bool) evse->getEvseState;
    //});

    ArduinoOcpp::setOnRemoteStartTransactionReceiveRequestListener([] (JsonObject payload) {

    });
}

unsigned long ArduinoOcppTask::loop(MicroTasks::WakeReason reason) {
    
#if DEBUG_OUT
    Serial.println("[ArduinoOcppTask] loop!");
#endif

    String dbg_msg = String('\0');
    dbg_msg += "EVSE state. getEvseState: ";
    dbg_msg += String(evse->getEvseState(), DEC);
    dbg_msg += ", isVehicleConnected: ";
    dbg_msg += evse->isVehicleConnected();
    dbg_msg += ", isCharging: ";
    dbg_msg += evse->isCharging();
    dbg_msg += ", isConnected: ";
    dbg_msg += evse->isConnected();
    dbg_msg += ", getState: ";
    dbg_msg += evse->getState();
    dbg_msg += ", getPilotState: ";
    dbg_msg += String(evse->getPilotState(), DEC);
    dbg_msg += " end";

    ArduinoOcpp::OcppOperation *debug_msg = ArduinoOcpp::makeOcppOperation(new ArduinoOcpp::Ocpp16::DataTransfer(dbg_msg));
    ArduinoOcpp::initiateOcppOperation(debug_msg);

    return 10000;
}

