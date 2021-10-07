/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#include "ocpp.h"

#include "app_config.h"

#include <ArduinoOcpp.h> // Facade for ArduinoOcpp
#include <ArduinoOcpp/SimpleOcppOperationFactory.h> // define behavior for incoming req messages

#include <ArduinoOcpp/Core/OcppEngine.h>

#include "emonesp.h" //for DEFAULT_VOLTAGE

#include "net_manager.h" //shut down network connection before reset
#include "espal.h"

#define LCD_DISPLAY(X) if (lcd) lcd->display((X), 0, 1, 5 * 1000, LCD_CLEAR_LINE);


ArduinoOcppTask *ArduinoOcppTask::instance = NULL;

ArduinoOcppTask::ArduinoOcppTask() : MicroTasks::Task() /*, bootReadyCallback(MicroTasksCallback([](){})) */ {
    
}

ArduinoOcppTask::~ArduinoOcppTask() {
    if (ocppSocket != NULL) delete ocppSocket;
    instance = NULL;
}

void ArduinoOcppTask::begin(EvseManager &evse, LcdTask &lcd) {
    
    this->evse = &evse;
    this->lcd = &lcd;

    initializeArduinoOcpp();
    loadEvseBehavior();

    instance = this; //cannot be in constructer because object is invalid before .begin()
    MicroTask.startTask(this);
}

void ArduinoOcppTask::initializeArduinoOcpp() {

    if (config_ocpp_enabled() && !arduinoOcppInitialized) {

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

        DynamicJsonDocument *evseDetailsDoc = new DynamicJsonDocument(JSON_OBJECT_SIZE(6));
        JsonObject evseDetails = evseDetailsDoc->to<JsonObject>();
        evseDetails["chargePointModel"] = "Advanced Series";
        //evseDetails["chargePointSerialNumber"] = "TODO";
        evseDetails["chargePointVendor"] = "OpenEVSE";
        evseDetails["firmwareVersion"] = evse->getFirmwareVersion();
        //evseDetails["meterSerialNumber"] = "TODO";
        //evseDetails["meterType"] = "TODO";

        bootNotification(evseDetailsDoc, [lcd = lcd](JsonObject payload) { //ArduinoOcpp will delete evseDetailsDoc
            LCD_DISPLAY("OCPP connected!");
        });

        arduinoOcppInitialized = true;
    }
}

void ArduinoOcppTask::setup() {

}

void ArduinoOcppTask::loadEvseBehavior() {

    if (!arduinoOcppInitialized) {
        return;
    }

    /*
     * Synchronize OpenEVSE data with OCPP-library data
     */

    setPowerActiveImportSampler([evse = evse]() {
        return (float) (evse->getAmps() * evse->getVoltage());
    });

    setEnergyActiveImportSampler([evse = evse] () {
        float activeImport = 0.f;
        activeImport += (float) evse->getTotalEnergy();
        activeImport += (float) evse->getSessionEnergy();
        return activeImport;
    });

    setOnChargingRateLimitChange([&charging_limit = charging_limit, this] (float limit) { //limit = maximum charge rate in Watts
        charging_limit = limit;
        this->updateEvseClaim();
    });

    setConnectorPluggedSampler([evse = evse] () {
        return (bool) evse->isConnected();
    });

    setEvRequestsEnergySampler([evse = evse] () {
        return (bool) evse->isCharging();
    });

    setConnectorEnergizedSampler([evse = evse] () {
        return evse->isActive();
    });

    /*
     * Report failures to central system. Note that the error codes are standardized in OCPP
     */

    addConnectorErrorCodeSampler([evse = evse] () {
        if (evse->getEvseState() == OPENEVSE_STATE_GFI_FAULT ||
                evse->getEvseState() == OPENEVSE_STATE_NO_EARTH_GROUND ||
                evse->getEvseState() == OPENEVSE_STATE_DIODE_CHECK_FAILED) {
            return "GroundFailure";
        }
        return (const char *) NULL;
    });

    addConnectorErrorCodeSampler([evse = evse] () {
        if (evse->getEvseState() == OPENEVSE_STATE_OVER_TEMPERATURE) {
            return "HighTemperature";
        }
        return (const char *) NULL;
    });

    addConnectorErrorCodeSampler([evse = evse] () {
        if (evse->getEvseState() == OPENEVSE_STATE_OVER_CURRENT) {
            return "OverCurrentFailure";
        }
        return (const char *) NULL;
    });

    addConnectorErrorCodeSampler([evse = evse] () {
        if (evse->getEvseState() == OPENEVSE_STATE_STUCK_RELAY ||
                evse->getEvseState() == OPENEVSE_STATE_GFI_SELF_TEST_FAILED) {
            return "InternalError";
        }
        return (const char *) NULL;
    });

    /*
     * CP behavior definition: How will plugging and unplugging the EV start or stop OCPP transactions
     */

    onVehicleConnect = [this] () {
        if (getTransactionId() < 0 && isAvailable()) {
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
            //RemoteStartTransaction rejected! Do nothing
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
            //RemoteStopTransaction rejected! There is no transaction with given ID. Do nothing
            return;
        }

        stopTransaction();
        this->updateEvseClaim();
    });

    setOnResetReceiveReq([this] (JsonObject payload) {
        const char *type = payload["type"] | "Soft";
        if (!strcmp(type, "Hard")) {
            resetHard = true;
        }

        resetTime = millis();
        resetTriggered = true;

        LCD_DISPLAY("Reboot EVSE");
    });

    setOnUnlockConnector([] () {
        //TODO Send unlock command to peripherals. If successful, return true, otherwise false
        return false;
    });

    updateEvseClaim();
}

unsigned long ArduinoOcppTask::loop(MicroTasks::WakeReason reason) {

    if (arduinoOcppInitialized) {
        if (config_ocpp_enabled()) {
            OCPP_loop();
        } else {
            //The OCPP function has been activated and then deactivated again.

            ArduinoOcpp::ocppEngine_loop(); //There could be remaining operations in the OCPP-queue. I will add the OCPP_unitialize() soon as a better solution to this
            return 0;
        }
    } else {
        if (config_ocpp_enabled()) {
            initializeArduinoOcpp();
            loadEvseBehavior();
        }
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

    if (resetTriggered) {
        if (millis() - resetTime >= 10000UL) { //wait for 10 seconds after reset command to send the conf msg
            if (resetHard) {
                //TODO send reset command to all peripherals
            }
            net_wifi_disconnect();
            ESPAL.reset();
        }
    }

    return 0;
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
    String url = ocpp_server;
    url.trim();
    if (url.isEmpty()) {
        return url; //return empty String
    }
    String chargeBoxId = ocpp_chargeBoxId;
    chargeBoxId.trim();
    if (!url.endsWith("/") && !chargeBoxId.isEmpty()) {
        url += '/';
    }
    url += chargeBoxId;

    if (MongooseOcppSocketClient::isValidUrl(url.c_str())) {
        return url;
    } else {
        DBUGLN(F("[ArduinoOcppTask] OCPP server URL or chargeBoxId invalid"));
        return String("");
    }
}

void ArduinoOcppTask::notifyConfigChanged() {
    if (instance) {
        instance->reconfigure();
    }
}

void ArduinoOcppTask::reconfigure() {
    if (arduinoOcppInitialized) {
        if (config_ocpp_enabled()) {
            String mUrl = getCentralSystemUrl();
            ocppSocket->reconnect(mUrl);
        } else {
            String emptyUrl = String("");
            ocppSocket->reconnect(emptyUrl);
        }
    } else {
        initializeArduinoOcpp();
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
