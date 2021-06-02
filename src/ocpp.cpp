/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#include "ocpp.h"

#include "debug.h"
#include "app_config.h"

#include <ArduinoOcpp.h> // Facade for ArduinoOcpp
#include <ArduinoOcpp/SimpleOcppOperationFactory.h> // define behavior for incoming req messages

#include <ArduinoOcpp/Core/OcppEngine.h> //only for outputting debug messages to SteVe
#include <ArduinoOcpp/MessagesV16/DataTransfer.h> //only for outputting debug messages to SteVe

#include "emonesp.h" //for DEFAULT_VOLTAGE

#define LCD_DISPLAY(X) if (lcd) lcd->display((X), 0, 1, 5 * 1000, LCD_CLEAR_LINE);


ArduinoOcppTask *ArduinoOcppTask::instance = NULL;

ArduinoOcppTask::ArduinoOcppTask() : MicroTasks::Task() /*, bootReadyCallback(MicroTasksCallback([](){})) */ {
    
}

ArduinoOcppTask::~ArduinoOcppTask() {
    if (ocppSocket != NULL) delete ocppSocket;
    instance = NULL;
}

void ArduinoOcppTask::begin(EvseManager &evse, LcdTask &lcd) {
    Serial.println("[ArduinoOcppTask] begin!");
    
    this->evse = &evse;
    this->lcd = &lcd;

    loadOcppLibrary();
    loadEvseBehavior();

    instance = this; //cannot be in constructer because object is invalid before .begin()
    MicroTask.startTask(this);
}

void ArduinoOcppTask::loadOcppLibrary() {

    if (config_ocpp_enabled() && !ocppLibraryLoaded) {

        String url = getCentralSystemUrl();

        if (url.isEmpty()) {
            return;
        }

        ocppSocket = new MongooseOcppSocketClient(url);

        ArduinoOcpp::OcppClock clockAdapter = [] () {
            timeval time_now;
            gettimeofday(&time_now, NULL);
            return (ArduinoOcpp::otime_t) time_now.tv_sec;
        };

        OCPP_initialize(ocppSocket, (float) DEFAULT_VOLTAGE, ArduinoOcpp::FilesystemOpt::Use, clockAdapter);

        bootNotification("Advanced Series", "OpenEVSE", [lcd = lcd](JsonObject payload) {
            LCD_DISPLAY("OCPP connected!");
        });

        ocppLibraryLoaded = true;
    }
}

void ArduinoOcppTask::setup() {

}

void ArduinoOcppTask::loadEvseBehavior() {

    if (!ocppLibraryLoaded) {
        return;
    }

    /*
     * Synchronize OpenEVSE data with OCPP-library data
     */

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

    setConnectorEnergizedSampler([evse = evse] () {
        return evse->isActive();
    });

    /*
     * CP behavior definition: How will plugging and unplugging the EV start or stop OCPP transactions
     */

    if (!config_ocpp_enabled()) {
        //ocpp off
        onVehicleConnect = [] () {};
        onVehicleDisconnect = [] () {};
        inferClaimTransactionActive = [] (EvseState& evseState, EvseProperties& evseProperties) {};
        inferClaimTransactionActiveOffline = [] (EvseState& evseState, EvseProperties& evseProperties) {};
        inferClaimTransactionInactive = [] (EvseState& evseState, EvseProperties& evseProperties) {};
        inferClaimSmartCharging = [evse = evse] (EvseState& evseState, EvseProperties& evseProperties, float charging_limit) {};
        updateEvseClaim();
        return;
    }

    onVehicleConnect = [this] () {
        if (getTransactionId() < 0) {
            if (!ocpp_idTag.isEmpty()) {
                authorize(ocpp_idTag, [this] (JsonObject payload) {
                    if (idTagIsAccepted(payload)) {
                        startTransaction([this] (JsonObject payload) {
                            this->updateEvseClaim();
                        }, [lcd = lcd] () {
                            LCD_DISPLAY("Central system error");
                        });
                    } else {
                        LCD_DISPLAY("ID card not recognized");
                    }
                }, [lcd = lcd] () {
                    LCD_DISPLAY("OCPP timeout");
                });
            } else {
                startTransaction([this] (JsonObject payload) {
                    if (!idTagIsRejected(payload)) {
                        this->updateEvseClaim();
                    } else {
                        LCD_DISPLAY("ID tag required");
                    }
                    this->updateEvseClaim();
                }, [lcd = lcd] () {
                    LCD_DISPLAY("Central system error");
                });
            }
            
        }
        this->updateEvseClaim();
    };

    setOnRemoteStartTransactionSendConf([this, onVehicleConnect = onVehicleConnect] (JsonObject payload) {
        if (!operationIsAccepted(payload)){
            if (DEBUG_OUT) Serial.print(F("RemoteStartTransaction rejected! Do nothing\n"));
            return;
        }

        startTransaction([this] (JsonObject payload) {
            this->updateEvseClaim();
        }, [lcd = lcd] () {
            LCD_DISPLAY("Central system error");
        });
    });

    onVehicleDisconnect = [this] () {
        if (getTransactionId() >= 0) {
            stopTransaction();
        }
        this->updateEvseClaim();
    };

    setOnRemoteStopTransactionSendConf([this](JsonObject payload) {
        if (!operationIsAccepted(payload)){
            if (DEBUG_OUT) Serial.print(F("RemoteStopTransaction rejected! There is no transaction with given ID. Do nothing\n"));
            return;
        }

        stopTransaction();
        this->updateEvseClaim();
    });

    updateEvseClaim();
}

unsigned long ArduinoOcppTask::loop(MicroTasks::WakeReason reason) {

    if (!config_ocpp_enabled()) {
        return 5000;
    }

    if (!ocppLibraryLoaded) {
        loadOcppLibrary();
        loadEvseBehavior();

        return 50;
    }

    if (evse->isVehicleConnected() && !vehicleConnected) {
        vehicleConnected = evse->isVehicleConnected();

        if (strcmp(tx_start_point.c_str(), "tx_only_remote")) {
            //tx_start_point is different from tx_only_remote. Plugging an EV starts a transaction
            onVehicleConnect();
        }
    }

    if (!evse->isVehicleConnected() && vehicleConnected) {
        vehicleConnected = evse->isVehicleConnected();

        onVehicleDisconnect();
    }

    return 50;
}

void ArduinoOcppTask::OcppLibrary_loop() {
    if (ocppLibraryLoaded) {
        if (config_ocpp_enabled()) {
            OCPP_loop();
        } else {
            ArduinoOcpp::ocppEngine_loop(); //better continue looping. Leftover pending messages could interfere with the networking stack 
        }
    }
}

void ArduinoOcppTask::updateEvseClaim() {

    EvseState evseState;
    EvseProperties evseProperties;

    int transactionId = getTransactionId(); //ID of OCPP-transaction. transactionId <  0 means that no transaction runs on the EVSE at the moment
                                            //                        transactionId >  0 means that the EVSE is in a charging transaction right now
                                            //                        transactionId == 0 means that the initiation is pending

    //EVSE is in an OCPP-transaction?
    if (transactionId < 0) {
        //no transaction running. Forbid charging
        evseState = EvseState::Disabled;
    } else if (transactionId == 0) {
        //transaction initiated but neither accepted nor rejected
        if (!strcmp(tx_start_point.c_str(), "tx_pending")) {
            evseState = EvseState::Active;
        } else {
            evseState = EvseState::Disabled;
        }
    } else {
        //transaction running. Allow charging
        evseState = EvseState::Active;
    }

    evseProperties = evseState;

    //OCPP Smart Charging?
    if (charging_limit < 0.f) {
        //OCPP Smart Charging is off. Nothing to do
    } else if (charging_limit >= -0.001f && charging_limit < 5.f) {
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

    if (evseState == EvseState::Disabled && !config_ocpp_access_can_suspend()) {
        //OCPP is configured to never put the EVSE into sleep
        evseState = EvseState::None;
        evseProperties = evseState;
    }

    if (evseState == EvseState::Active && !config_ocpp_access_can_energize()) {
        //OCPP is configured to never override the sleep mode of other services
        evseState = EvseState::None;
        evseProperties = evseState;
    }

    //Apply inferred claim
    if (evseState == EvseState::None || !config_ocpp_enabled()) {
        //the claiming rules don't specify the EVSE state
        evse->release(EvseClient_OpenEVSE_Ocpp);
    } else {
        //the claiming rules specify that the EVSE is either active or inactive
        evse->claim(EvseClient_OpenEVSE_Ocpp, EvseManager_Priority_Ocpp, evseProperties);
    }

}

String ArduinoOcppTask::getCentralSystemUrl() {
    ocpp_server.trim();
    String url = ocpp_server;
    if (!url.endsWith("/")) {
        url += '/';
    }
    ocpp_chargeBoxId.trim();
    url += ocpp_chargeBoxId;
    return url;
}

void ArduinoOcppTask::notifyReconfigured() {
    if (instance) {
        instance->reconfigure();
    }
}

void ArduinoOcppTask::reconfigure() {
    if (ocppLibraryLoaded) {
        if (config_ocpp_enabled()) {
            String mUrl = getCentralSystemUrl();
            ocppSocket->reconnect(mUrl);
        } else {
            String emptyUrl = String("");
            ocppSocket->reconnect(emptyUrl);
        }
    } else {
        loadOcppLibrary();
    }
    loadEvseBehavior();
}

bool ArduinoOcppTask::operationIsAccepted(JsonObject payload) {
    const char *status = payload["status"] | "Invalid";
    return !strcmp(status, "Accepted");
}

bool ArduinoOcppTask::idTagIsAccepted(JsonObject payload) {
    const char *status = payload["idTagInfo"]["status"] | "Invalid";
    return !strcmp(status, "Accepted");
}

bool ArduinoOcppTask::idTagIsRejected(JsonObject payload) {
    const char *status = payload["idTagInfo"]["status"] | "Accepted";
    return strcmp(status, "Accepted");
}
