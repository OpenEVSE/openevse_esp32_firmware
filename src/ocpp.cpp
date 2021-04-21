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

    onVehicleConnect = [this] () {
        startTransaction([this] (JsonObject payload) {
            this->updateEvseClaim();
        });
    };

    onVehicleDisconnect = [this] () {
        stopTransaction([this] (JsonObject payload) {
            this->updateEvseClaim();
        }, [this] () {
            this->updateEvseClaim();
        });
    };

    setOnRemoteStartTransactionSendConf([this] (JsonObject payload) {

        if (!operationIsAccepted(payload)){
            if (DEBUG_OUT) Serial.print(F("RemoteStartTransaction rejected! Do nothing\n"));
            return;
        }

        startTransaction([this] (JsonObject payload) {
            this->updateEvseClaim();
        });
    });

    setOnRemoteStopTransactionSendConf([this](JsonObject payload) {
        if (!operationIsAccepted(payload)){
            if (DEBUG_OUT) Serial.print(F("RemoteStopTransaction rejected! There is no transaction with given ID. Do nothing\n"));
            return;
        }

        stopTransaction();
        this->updateEvseClaim();
    });

    inferClaimTransactionActive = [] (EvseState& evseState, EvseProperties& evseProperties) {
        evseState = EvseState::Active;
        evseProperties.setState(evseState);
    };

    inferClaimTransactionInactive = [] (EvseState& evseState, EvseProperties& evseProperties) {
        evseState = EvseState::Disabled;
        evseProperties.setState(evseState);
    };

    inferClaimSmartCharging = [evse = evse] (EvseState& evseState, EvseProperties& evseProperties, float charging_limit) {
        if (charging_limit < 0.f) {
            //OCPP Smart Charging is off. Nothing to do
        } else if (charging_limit >= 0.f && charging_limit < 5.f) {
            //allowed charge rate is "equal or almost equal" to 0W
            evseState = EvseState::Disabled; //override state
            evseProperties.setState(evseState); //renew properties
        } else {
            //charge rate is valid. Set charge rate
            float volts = evse->getVoltage(); // convert Watts to Amps. TODO Maybe use "smoothed" voltage value?
            if (volts > 0) {
                float amps = charging_limit / volts;
                evseProperties.setChargeCurrent(amps);
            }
        }
    };

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

        onVehicleConnect();
    }

    if (!evse->isVehicleConnected() && vehicleConnected) {
        vehicleConnected = evse->isVehicleConnected();

        //transition: EV plugged -> no EV plugged

        onVehicleDisconnect();
    }

    //return 1;
    return 10000; //increase for debugging
}

void ArduinoOcppTask::updateEvseClaim() {
#if 0 // Simpler approach. Equivalent algorithm with strategy pattern in else clause (preferred)
    EvseState evseState;
    EvseProperties evseProperties;

    int transactionId = getTransactionId(); //ID of OCPP-transaction. transactionId <= 0 means that no transaction runs on the EVSE at the moment
                                            //                        transactionId >  0 means that the EVSE is in a charging transaction right now
                                            //                        transactionId == 0 is invalid

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
#else

    EvseState evseState = EvseState::None;
    EvseProperties evseProperties = evseState;
    
    int transactionId = getTransactionId(); //ID of OCPP-transaction. transactionId <= 0 means that no transaction runs on the EVSE at the moment
                                            //                        transactionId >  0 means that the EVSE is in a charging transaction right now
                                            //                        transactionId == 0 is invalid

    //EVSE is in an OCPP-transaction?
    if (transactionId < 0) {
        //no transaction running. Forbid charging
        inferClaimTransactionInactive(evseState, evseProperties);
    } else {
        //transaction running or transactionId invalid. Allow charging
        inferClaimTransactionActive(evseState, evseProperties);
    }

    //OCPP Smart Charging?
    inferClaimSmartCharging(evseState, evseProperties, charging_limit);

    //Apply inferred claim
    if (evseState == EvseState::None) {
        //the claiming rules don't specify the EVSE state
        evse->release(EvseClient_OpenEVSE_Ocpp);
    } else {
        //the claiming rules specify that the EVSE is either active or inactive
        evse->claim(EvseClient_OpenEVSE_Ocpp, EvseManager_Priority_Ocpp, evseProperties);
    }

#endif
}

bool ArduinoOcppTask::operationIsAccepted(JsonObject payload) {
    return !strcmp(payload["status"], "Accepted");
}
