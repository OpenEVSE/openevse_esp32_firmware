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

    setOnChargingRateLimitChange([&charging_limit = charging_limit, this] (float limit) { //limit = maximum charge rate in Watts
        charging_limit = limit;
        this->updateEvseClaim();
    });

    setEvRequestsEnergySampler([evse = evse] () {
        return (bool) evse->isCharging();
    });

//    setOnRemoteStartTransactionReceiveRequest([this, &transactionId = transactionId] (JsonObject payload) {
//
//        String idTag = payload["idTag"].as<String>(); 
//        if (!idTag.isEmpty()) {
//            ArduinoOcpp::getChargePointStatusService()->authorize(idTag); //TODO maybe ArduinoOcpp should already have done that here
//        }
//    });

    setOnRemoteStartTransactionSendConf([this, &transactionId = transactionId] (JsonObject payload) {

        if (!operationIsAccepted(payload)){
            if (DEBUG_OUT) Serial.print(F("RemoteStartTransaction rejected! Do nothing\n"));
            return;
        }

        startTransaction([this, &transactionId = transactionId] (JsonObject payload) {
            transactionId = getTransactionId();
            this->updateEvseClaim();
        });
    });

    setOnRemoteStopTransactionSendConf([this, &transactionId = transactionId](JsonObject payload) {
        if (!operationIsAccepted(payload)){
            if (DEBUG_OUT) Serial.print(F("RemoteStopTransaction rejected! There is no transaction with given ID. Do nothing\n"));
            return;
        }

        stopTransaction();

        transactionId = getTransactionId();
        this->updateEvseClaim();
    });

    updateEvseClaim();
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

    if (evse->isVehicleConnected() && !vehicleConnected) {
        vehicleConnected = evse->isVehicleConnected();

        //transition: no EV plugged -> EV plugged

        startTransaction([this, &transactionId = transactionId] (JsonObject payload) {
            transactionId = getTransactionId();
            this->updateEvseClaim();
        });
    };

    if (!evse->isVehicleConnected() && vehicleConnected) {
        vehicleConnected = evse->isVehicleConnected();

        //transition: EV plugged -> no EV plugged

        stopTransaction();
        transactionId = getTransactionId();
        this->updateEvseClaim();
    }

    return 10000;
}

void ArduinoOcppTask::updateEvseClaim() {
    EvseState evseState;
    EvseProperties evseProperties;

    //EVSE is in an OCPP-transaction?
    if (transactionId < 0) {
        //no transaction running. Forbid charging
        evseState = EvseState::Disabled;
    } else {
        //transaction running or transactionId invalid. Allow charging
        evseState = EvseState::Active;
    }

    evseProperties = evseState;

    //OCPP Smart Charging?
    if (charging_limit < 0.f) {
        //OCPP Smart Charging is off. Nothing to do
    } else if (charging_limit >= 0.f && charging_limit < 50.f) {
        //allowed charge rate is "equal or almost equal" to 0W
        evseState = EvseState::Disabled; //override state
        evseProperties = evseState; //renew properties
    } else {
        //charge rate is valid. Set charge rate
        float volts = evse->getVoltage(); // convert Watts to Amps. TODO Maybe use "smoothed" voltage value?
        if (volts > 0) {
            float amps = charging_limit / volts;
            evseProperties.setChargeCurrent(amps);
        }
    }

    evse->claim(EvseClient_OpenEVSE_Ocpp, EvseManager_Priority_Ocpp, evseProperties);
}

bool ArduinoOcppTask::operationIsAccepted(JsonObject payload) {
    return !strcmp(payload["status"], "Accepted");
}
