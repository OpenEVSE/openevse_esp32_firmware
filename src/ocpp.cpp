/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#include "ocpp.h"

#include <ArduinoOcpp.h>
#include <ArduinoOcpp/SimpleOcppOperationFactory.h>
#include <ArduinoOcpp/Core/OcppEngine.h>
#include <ArduinoOcpp/Platform.h>

#include "app_config.h"
#include "http_update.h"
#include "emonesp.h"

#define LCD_DISPLAY(X) if (lcd) lcd->display((X), 0, 1, 5 * 1000, LCD_CLEAR_LINE);


ArduinoOcppTask *ArduinoOcppTask::instance = NULL;

void dbug_wrapper(const char *msg) {
    DBUG(msg);
}

ArduinoOcppTask::ArduinoOcppTask() : MicroTasks::Task() {

}

ArduinoOcppTask::~ArduinoOcppTask() {
    if (ocppSocket != NULL) delete ocppSocket;
    instance = NULL;
}

void ArduinoOcppTask::begin(EvseManager &evse, LcdTask &lcd, EventLog &eventLog, RfidTask &rfid) {

    this->evse = &evse;
    this->lcd = &lcd;
    this->eventLog = &eventLog;
    this->rfid = &rfid;

    ao_set_console_out(dbug_wrapper);

    reconfigure();

    instance = this; //cannot be in constructer because object is invalid before .begin()
    MicroTask.startTask(this);
}

void ArduinoOcppTask::reconfigure() {

    if (arduinoOcppInitialized) {
        arduinoOcppInitialized = false;

        if (!config_ocpp_enabled() && ocppSocket) {
            String emptyUrl = String("");
            ocppSocket->reconnect(emptyUrl);
        }
        OCPP_deinitialize();
    }

    if (config_ocpp_enabled()) {
        String url = getCentralSystemUrl();

        if (ocppSocket) {
            ocppSocket->reconnect(url);
        } else {
            ocppSocket = new MongooseOcppSocketClient(url);
        }

        initializeArduinoOcpp();

        arduinoOcppInitialized = true;
    }
}

void ArduinoOcppTask::initializeArduinoOcpp() {

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

    OCPP_initialize(*ocppSocket, (float) VOLTAGE_DEFAULT, ArduinoOcpp::FilesystemOpt::Use, clockAdapter);

    loadEvseBehavior();
    initializeDiagnosticsService();
    initializeFwService();

    /*
     * BootNotification: provide the OCPP backend with relevant data about the OpenEVSE
     * see https://github.com/OpenEVSE/ESP32_WiFi_V4.x/issues/219
     */
    String evseFirmwareVersion = String(evse->getFirmwareVersion());

    DynamicJsonDocument *evseDetailsDoc = new DynamicJsonDocument(
        JSON_OBJECT_SIZE(5)
        + serial.length() + 1
        + currentfirmware.length() + 1
        + evseFirmwareVersion.length() + 1);
    JsonObject evseDetails = evseDetailsDoc->to<JsonObject>();
    evseDetails["chargePointModel"] = "Advanced Series";
    evseDetails["chargePointSerialNumber"] = serial; //see https://github.com/OpenEVSE/ESP32_WiFi_V4.x/issues/218
    evseDetails["chargePointVendor"] = "OpenEVSE";
    evseDetails["firmwareVersion"] = currentfirmware;
    evseDetails["meterSerialNumber"] = evseFirmwareVersion;

    bootNotification(evseDetailsDoc, [this](JsonObject payload) { //ArduinoOcpp will delete evseDetailsDoc
        LCD_DISPLAY("OCPP connected!");
    });

    ocppTxIdDisplay = getTransactionId();
    ocppSessionDisplay = getSessionIdTag();
}

void ArduinoOcppTask::setup() {

}

void ArduinoOcppTask::loadEvseBehavior() {

    /*
     * Synchronize OpenEVSE data with OCPP-library data
     */

    setPowerActiveImportSampler([this]() {
        return (float) (evse->getAmps() * evse->getVoltage());
    });

    setEnergyActiveImportSampler([this] () {
        float activeImport = 0.f;
        activeImport += (float) evse->getTotalEnergy();
        activeImport += (float) evse->getSessionEnergy();
        return activeImport;
    });

    setOnChargingRateLimitChange([this] (float limit) { //limit = maximum charge rate in Watts
        charging_limit = limit;
    });

    setConnectorPluggedSampler([this] () {
        return (bool) evse->isVehicleConnected();
    });

    setEvRequestsEnergySampler([this] () {
        return (bool) evse->isCharging();
    });

    setConnectorEnergizedSampler([this] () {
        return evse->isActive();
    });

    /*
     * Report failures to central system. Note that the error codes are standardized in OCPP
     */

    addConnectorErrorCodeSampler([this] () {
        if (evse->getEvseState() == OPENEVSE_STATE_GFI_FAULT ||
                evse->getEvseState() == OPENEVSE_STATE_GFI_SELF_TEST_FAILED ||
                evse->getEvseState() == OPENEVSE_STATE_NO_EARTH_GROUND ||
                evse->getEvseState() == OPENEVSE_STATE_DIODE_CHECK_FAILED) {
            return "GroundFailure";
        }
        return (const char *) NULL;
    });

    addConnectorErrorCodeSampler([this] () {
        if (evse->getEvseState() == OPENEVSE_STATE_OVER_TEMPERATURE) {
            return "HighTemperature";
        }
        return (const char *) NULL;
    });

    addConnectorErrorCodeSampler([this] () {
        if (evse->getEvseState() == OPENEVSE_STATE_OVER_CURRENT) {
            return "OverCurrentFailure";
        }
        return (const char *) NULL;
    });

    addConnectorErrorCodeSampler([this] () {
        if (evse->getEvseState() == OPENEVSE_STATE_STUCK_RELAY) {
            return "PowerSwitchFailure";
        }
        return (const char *) NULL;
    });

    addConnectorErrorCodeSampler([this] () {
        if (rfid->communicationFails()) {
            return "ReaderFailure";
        }
        return (const char *) nullptr;
    });

    /*
     * CP behavior definition: How will plugging and unplugging the EV start or stop OCPP transactions
     */

    onIdTagInput = [this] (const String& idInput) {
        if (!config_ocpp_enabled()) {
            return false;
        }
        if (idInput.isEmpty()) {
            DBUGLN("[ocpp] empty idTag");
            return true;
        }
        if (!isAvailable() || !arduinoOcppInitialized) {
            LCD_DISPLAY("OCPP inoperative");
            DBUGLN(F("[ocpp] present card but inoperative"));
            return true;
        }
        const char *sessionIdTag = getSessionIdTag();
        if (sessionIdTag) {
            //currently in an authorized session
            if (idInput.equals(sessionIdTag)) {
                //NFC card matches
                endSession();
                LCD_DISPLAY("Card accepted");
            } else {
                LCD_DISPLAY("Card not recognized");
            }
        } else {
            //idle mode
            LCD_DISPLAY("Card read");
            String idInputCapture = idInput;
            authorize(idInput.c_str(), [this, idInputCapture] (JsonObject payload) {
                if (idTagIsAccepted(payload)) {
                    beginSession(idInputCapture.c_str());
                    LCD_DISPLAY("Card accepted");
                    if (!evse->isVehicleConnected()) {
                        LCD_DISPLAY("Plug in cable");
                    }
                } else {
                    LCD_DISPLAY("Card not recognized");
                }
            }, [this] () {
                LCD_DISPLAY("OCPP timeout");
            });
        }

        return true;
    };

    rfid->setOnCardScanned(&onIdTagInput);

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
        //see https://github.com/OpenEVSE/ESP32_WiFi_V4.x/issues/230
        return false;
    });
}

unsigned long ArduinoOcppTask::loop(MicroTasks::WakeReason reason) {

    if (arduinoOcppInitialized) {
        OCPP_loop();
    }
    if (arduinoOcppInitialized != config_ocpp_enabled()) {
        reconfigure();
        return 50;
    }

    if (evse->isVehicleConnected() && !vehicleConnected) {
        vehicleConnected = evse->isVehicleConnected();

        if (arduinoOcppInitialized && !config_rfid_enabled()) {
            //no rfid reader --> sessions will be started by plugging the EV or by RemoteStartTransaction
            if (strcmp(tx_start_point.c_str(), "tx_only_remote")) {
                //tx_start_point is different from tx_only_remote. Plugging an EV begins a session
                if (!getSessionIdTag()) { //check if session has already begun
                    beginSession(ocpp_idTag.isEmpty() ? "A0-00-00-00" : ocpp_idTag.c_str());
                }
            }
        }

        if (!getSessionIdTag()) {
            if (config_rfid_enabled()) {
                LCD_DISPLAY("Need card");
            } else {
                LCD_DISPLAY("Please authorize session");
            }
        }
    }

    if (!evse->isVehicleConnected() && vehicleConnected) {
        vehicleConnected = evse->isVehicleConnected();
    }

    if (ocppSessionDisplay && !getSessionIdTag()) {
        //Session unauthorized. Show if StartTransaction didn't succeed
        if (ocppTxIdDisplay < 0) {
            LCD_DISPLAY("Card timeout");
            LCD_DISPLAY("Present card again");
        }
    }
    ocppSessionDisplay = getSessionIdTag();

    if (ocppTxIdDisplay <= 0 && getTransactionId() > 0) {
        LCD_DISPLAY("OCPP start tx");
        String txIdMsg = "TxID ";
        txIdMsg += String(getTransactionId());
        LCD_DISPLAY(txIdMsg);
    } else if (ocppTxIdDisplay > 0 && getTransactionId() < 0) {
        LCD_DISPLAY("OCPP Good bye!");
        String txIdMsg = "TxID ";
        txIdMsg += String(ocppTxIdDisplay);
        txIdMsg += " finished";
        LCD_DISPLAY(txIdMsg);
    }
    ocppTxIdDisplay = getTransactionId();

    if (resetTriggered) {
        if (millis() - resetTime >= 10000UL) { //wait for 10 seconds after reset command to send the conf msg
            resetTriggered = false; //execute only once

            if (resetHard) {
                //TODO send reset command to all peripherals
                //see https://github.com/OpenEVSE/ESP32_WiFi_V4.x/issues/228
            }
            
            restart_system();
        }
    }

    if (millis() - updateEvseClaimLast >= 1009) {
        updateEvseClaimLast = millis();
        updateEvseClaim();
    }

    return arduinoOcppInitialized ? 0 : 1000;
}

void ArduinoOcppTask::updateEvseClaim() {

    EvseState evseState;
    EvseProperties evseProperties;

    if (getTransactionId() == 0 && !strcmp(tx_start_point.c_str(), "tx_pending")) {
        //transaction initiated but neither accepted nor rejected
        evseState = EvseState::Active;
    } else if (ocppPermitsCharge()) {
        evseState = EvseState::Active;
    } else {
        evseState = EvseState::Disabled;
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

void ArduinoOcppTask::initializeDiagnosticsService() {
    ArduinoOcpp::DiagnosticsService *diagService = getDiagnosticsService();
    if (diagService) {
        diagService->setOnUploadStatusSampler([this] () {
            if (diagFailure) {
                return ArduinoOcpp::UploadStatus::UploadFailed;
            } else if (diagSuccess) {
                return ArduinoOcpp::UploadStatus::Uploaded;
            } else {
                return ArduinoOcpp::UploadStatus::NotUploaded;
            }
        });

        diagService->setOnUpload([this] (const std::string &location, ArduinoOcpp::OcppTimestamp &startTime, ArduinoOcpp::OcppTimestamp &stopTime) {
            
            //reset reported state
            diagSuccess = false;
            diagFailure = false;

            //check if input URL is valid
            unsigned int port_i = 0;
            struct mg_str scheme, query, fragment;
            if (mg_parse_uri(mg_mk_str(location.c_str()), &scheme, NULL, NULL, &port_i, NULL, &query, &fragment)) {
                DBUG(F("[ocpp] Diagnostics upload, invalid URL: "));
                DBUGLN(location.c_str());
                diagFailure = true;
                return false;
            }

            if (eventLog == NULL) {
                diagFailure = true;
                return false;
            }

            //create file to upload
            #define BOUNDARY_STRING "-----------------------------WebKitFormBoundary7MA4YWxkTrZu0gW025636501"
            const char *bodyPrefix PROGMEM = BOUNDARY_STRING "\r\n"
                    "Content-Disposition: form-data; name=\"file\"; filename=\"diagnostics.log\"\r\n"
                    "Content-Type: application/octet-stream\r\n\r\n";
            const char *bodySuffix PROGMEM = "\r\n\r\n" BOUNDARY_STRING "--\r\n";
            const char *overflowMsg PROGMEM = "{\"diagnosticsMsg\":\"requested search period exceeds maximum diagnostics upload size\"}";

            const size_t MAX_BODY_SIZE = 10000; //limit length of message
            String body = String('\0');
            body.reserve(MAX_BODY_SIZE);
            body += bodyPrefix;
            body += "[";
            const size_t SUFFIX_RESERVED_AREA = MAX_BODY_SIZE - strlen(bodySuffix) - strlen(overflowMsg) - 2;

            bool firstEntry = true;
            bool overflow = false;
            for (uint32_t i = 0; i <= (eventLog->getMaxIndex() - eventLog->getMinIndex()) && !overflow; i++) {
                uint32_t index = eventLog->getMinIndex() + i;

                eventLog->enumerate(index, [this, startTime, stopTime, &body, SUFFIX_RESERVED_AREA, &firstEntry, &overflow] (String time, EventType type, const String &logEntry, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint32_t pilot, double energy, uint32_t elapsed, double temperature, double temperatureMax, uint8_t divertMode) {
                    if (overflow) return;
                    ArduinoOcpp::OcppTimestamp timestamp = ArduinoOcpp::OcppTimestamp();
                    if (!timestamp.setTime(time.c_str())) {
                        DBUG(F("[ocpp] Diagnostics upload, cannot parse timestamp format: "));
                        DBUGLN(time);
                        return;
                    }

                    if (timestamp < startTime || timestamp > stopTime) {
                        return;
                    }

                    if (body.length() + logEntry.length() + 10 < SUFFIX_RESERVED_AREA) {
                        if (firstEntry)
                            firstEntry = false;
                        else
                            body += ",";
                        
                        body += logEntry;
                        body += "\n";
                    } else {
                        overflow = true;
                        return;
                    }
                });
            }

            if (overflow) {
                if (!firstEntry)
                    body += ",\r\n";
                body += overflowMsg;
            }

            body += "]";

            body += bodySuffix;

            DBUG(F("[ocpp] POST diagnostics file to "));
            DBUGLN(location.c_str());

            MongooseHttpClientRequest *request =
                    diagClient.beginRequest(location.c_str());
            request->setMethod(HTTP_POST);
            request->addHeader("Content-Type", "multipart/form-data; boundary=" BOUNDARY_STRING);
            request->setContent(body.c_str());
            request->onResponse([this] (MongooseHttpClientResponse *response) {
                if (response->respCode() == 200) {
                    diagSuccess = true;
                } else {
                    diagFailure = true;
                }
            });
            request->onClose([this] () {
                if (!diagSuccess) {
                    //triggered onClose before onResponse
                    diagFailure = true;
                }
            });
            diagClient.send(request);
            
            return true;
        });
    }
}

void ArduinoOcppTask::initializeFwService() {
    ArduinoOcpp::FirmwareService *fwService = getFirmwareService();
    if (fwService) {
        fwService->setBuildNumber(evse->getFirmwareVersion());

        fwService->setInstallationStatusSampler([this] () {
            if (updateFailure) {
                return ArduinoOcpp::InstallationStatus::InstallationFailed;
            } else if (updateSuccess) {
                return ArduinoOcpp::InstallationStatus::Installed;
            } else {
                return ArduinoOcpp::InstallationStatus::NotInstalled;
            }
        });

        fwService->setOnInstall([this](const std::string &location) {

            DBUGLN(F("[ocpp] Starting installation routine"));
            
            //reset reported state
            updateFailure = false;
            updateSuccess = false;

            return http_update_from_url(String(location.c_str()), [] (size_t complete, size_t total) { },
                [this] (int status_code) {
                    //onSuccess
                    updateSuccess = true;

                    resetTime = millis();
                    resetTriggered = true;
                }, [this] (int error_code) {
                    //onFailure
                    updateFailure = true;
                });
        });
    }
}

bool ArduinoOcppTask::idTagIsAccepted(JsonObject payload) {
    const char *status = payload["idTagInfo"]["status"] | "Invalid";
    return !strcmp(status, "Accepted");
}
