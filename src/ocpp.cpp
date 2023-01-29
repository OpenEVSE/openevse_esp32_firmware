/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#include "ocpp.h"

#include <ArduinoOcpp.h>
#include <ArduinoOcpp/SimpleOcppOperationFactory.h>
#include <ArduinoOcpp/Core/OcppEngine.h>
#include <ArduinoOcpp/Platform.h>
#include <MongooseCore.h>

#include "app_config.h"
#include "http_update.h"
#include "emonesp.h"
#include "root_ca.h"

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
        bootNotificationAccepted = false;

        if (!config_ocpp_enabled() && ocppSocket) {
            delete ocppSocket;
            ocppSocket = nullptr;
        }
        OCPP_deinitialize();
    }

    if (config_ocpp_enabled()) {
        if (ocppSocket) {
            //update URL storage with credentials from UI
            ocppSocket->setBackendUrl(ocpp_server.c_str());
            ocppSocket->setChargeBoxId(ocpp_chargeBoxId.c_str());
            ocppSocket->setAuthKey(ocpp_authkey.c_str());
            ocppSocket->reconnect();
        } else {
            ocppSocket = new ArduinoOcpp::AOcppMongooseClient(Mongoose.getMgr(),
                    ocpp_server.c_str(), //fallback URL. Normally, OcppSocket loads URL from own store
                    ocpp_chargeBoxId.c_str(),
                    ocpp_authkey.c_str(),
                    root_ca, //defined in root_ca.cpp
                    ArduinoOcpp::makeDefaultFilesystemAdapter(ArduinoOcpp::FilesystemOpt::Use));
            
            //override values in UI with URL storage. Unfortunately, editing URL in UI and enabling OCPP at the same time
            //is not possible
            bool updated = !ocpp_server.equals(ocppSocket->getBackendUrl()) ||
                    !ocpp_chargeBoxId.equals(ocppSocket->getChargeBoxId()) ||
                    !ocpp_authkey.equals(ocppSocket->getAuthKey());
            
            if (updated) {
                DynamicJsonDocument updateQuery (JSON_OBJECT_SIZE(3)); //use JSON in no-copy mode
                updateQuery["ocpp_server"] = ocppSocket->getBackendUrl();
                updateQuery["ocpp_chargeBoxId"] = ocppSocket->getChargeBoxId();
                updateQuery["ocpp_authkey"] = ocppSocket->getAuthKey();
                config_deserialize(updateQuery);
                config_commit();
            }
        }

        initializeArduinoOcpp();

        arduinoOcppInitialized = true;
    }
}

void ArduinoOcppTask::initializeArduinoOcpp() {

    OCPP_initialize(*ocppSocket, (float) VOLTAGE_DEFAULT, ArduinoOcpp::FilesystemOpt::Use);

    loadEvseBehavior();
    initializeDiagnosticsService();
    initializeFwService();

    /*
     * BootNotification: provide the OCPP backend with relevant data about the OpenEVSE
     * see https://github.com/OpenEVSE/ESP32_WiFi_V4.x/issues/219
     */
    String evseFirmwareVersion = String(evse->getFirmwareVersion());

    auto evseDetailsDoc = std::unique_ptr<DynamicJsonDocument>(new DynamicJsonDocument(
        JSON_OBJECT_SIZE(5)
        + serial.length() + 1
        + currentfirmware.length() + 1
        + evseFirmwareVersion.length() + 1));
    JsonObject evseDetails = evseDetailsDoc->to<JsonObject>();
    evseDetails["chargePointModel"] = "Advanced Series";
    evseDetails["chargePointSerialNumber"] = serial; //see https://github.com/OpenEVSE/ESP32_WiFi_V4.x/issues/218
    evseDetails["chargePointVendor"] = "OpenEVSE";
    evseDetails["firmwareVersion"] = currentfirmware;
    evseDetails["meterSerialNumber"] = evseFirmwareVersion;

    bootNotification(std::move(evseDetailsDoc), [this](JsonObject response) { //ArduinoOcpp will delete evseDetailsDoc
        if (response["status"].as<String>().equals("Accepted")) {
            LCD_DISPLAY("OCPP connected!");
            bootNotificationAccepted = true;
        } else {
            LCD_DISPLAY("OCPP refused EVSE");
        }
    });

    ocppTxIdDisplay = getTransactionId();
    ocppSessionDisplay = getTransactionIdTag();
}

void ArduinoOcppTask::setup() {

}

void ArduinoOcppTask::loadEvseBehavior() {

    /*
     * Synchronize OpenEVSE data with OCPP-library data
     */

    addMeterValueInput([this] () {
            return (int32_t) (evse->getAmps() * evse->getVoltage());
        },
        "Power.Active.Import",
        "W");
    
    addMeterValueInput([this] () {
            float activeImport = 0.f;
            activeImport += (float) evse->getTotalEnergy();
            activeImport += (float) evse->getSessionEnergy();
            return (int32_t) activeImport;
        }, 
        "Energy.Active.Import.Register",
        "Wh");

    addMeterValueInput([this] () {
            return (int32_t) evse->getAmps();
        }, 
        "Current.Import",
        "A");

    addMeterValueInput([this] () {
            return (int32_t) charging_limit;
        }, 
        "Current.Offered",
        "A");
    
    addMeterValueInput([this] () {
            return (int32_t) evse->getVoltage();
        }, 
        "Voltage",
        "V");
    
    addMeterValueInput([this] () {
            return (int32_t) evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR);
        }, 
        "Temperature",
        "C");

    auto patchChargingProfileUnit = ArduinoOcpp::declareConfiguration<const char*>("OE_CSProfileUnitMode", "W", CONFIGURATION_FN, false, false);

    setSmartChargingOutput([this, patchChargingProfileUnit] (float limit) { //limit = maximum charge rate in Watts
        if (patchChargingProfileUnit &&
                ((*patchChargingProfileUnit)[0] == 'a' || (*patchChargingProfileUnit)[0] == 'A')) {
            charging_limit = limit; //already A
        } else {
            charging_limit = limit / VOLTAGE_DEFAULT; //convert W to A
        }
    });

    setConnectorPluggedInput([this] () {
        return (bool) evse->isVehicleConnected();
    });

    setEvReadyInput([this] () {
        return (bool) evse->isCharging();
    });

    setEvseReadyInput([this] () {
        return evse->isActive();
    });

    /*
     * Report failures to central system. Note that the error codes are standardized in OCPP
     */

    addErrorCodeInput([this] () {
        if (evse->getEvseState() == OPENEVSE_STATE_GFI_FAULT ||
                evse->getEvseState() == OPENEVSE_STATE_GFI_SELF_TEST_FAILED ||
                evse->getEvseState() == OPENEVSE_STATE_NO_EARTH_GROUND ||
                evse->getEvseState() == OPENEVSE_STATE_DIODE_CHECK_FAILED) {
            return "GroundFailure";
        }
        return (const char *) NULL;
    });

    addErrorCodeInput([this] () {
        if (evse->getEvseState() == OPENEVSE_STATE_OVER_TEMPERATURE) {
            return "HighTemperature";
        }
        return (const char *) NULL;
    });

    addErrorCodeInput([this] () {
        if (evse->getEvseState() == OPENEVSE_STATE_OVER_CURRENT) {
            return "OverCurrentFailure";
        }
        return (const char *) NULL;
    });

    addErrorCodeInput([this] () {
        if (evse->getEvseState() == OPENEVSE_STATE_STUCK_RELAY) {
            return "PowerSwitchFailure";
        }
        return (const char *) NULL;
    });

    addErrorCodeInput([this] () {
        if (rfid->communicationFails()) {
            return "ReaderFailure";
        }
        return (const char *) nullptr;
    });

    /*
     * CP behavior definition: How will plugging and unplugging the EV start or stop OCPP transactions
     */

    freevendActive = ArduinoOcpp::declareConfiguration<bool>("AO_FreeVendActive", false, CONFIGURATION_FN);
    freevendIdTag = ArduinoOcpp::declareConfiguration<const char*>("AO_FreeVendIdTag", "", CONFIGURATION_FN);
    allowOfflineTxForUnknownId = ArduinoOcpp::declareConfiguration<bool>("AllowOfflineTxForUnknownId", false, CONFIGURATION_FN);

    if (!*freevendActive && config_ocpp_auto_authorization()) {
        //recommended to stop capturing transactions when being offline in Freevend mode
        auto silentOfflineTx = ArduinoOcpp::declareConfiguration<bool>("AO_SilentOfflineTransactions", false, CONFIGURATION_FN);
        *silentOfflineTx = true;
    }

    *freevendActive = config_ocpp_auto_authorization();
    *freevendIdTag = ocpp_idtag.c_str();
    *allowOfflineTxForUnknownId = config_ocpp_offline_authorization();

    ArduinoOcpp::configuration_save();
    
    trackConfigRevision = freevendActive->getValueRevision() +
                          freevendIdTag->getValueRevision() +
                          allowOfflineTxForUnknownId->getValueRevision();

    onIdTagInput = [this] (const String& idInput) {
        if (!config_ocpp_enabled()) {
            return false;
        }
        if (idInput.isEmpty()) {
            DBUGLN("[ocpp] empty idTag");
            return true;
        }
        if (!isOperative() || !arduinoOcppInitialized) {
            LCD_DISPLAY("OCPP inoperative");
            DBUGLN(F("[ocpp] present card but inoperative"));
            return true;
        }
        const char *sessionIdTag = getTransactionIdTag();
        if (sessionIdTag) {
            //currently in an authorized session
            if (idInput.equals(sessionIdTag)) {
                //NFC card matches
                endTransaction("Local");
                LCD_DISPLAY("Card accepted");
            } else {
                LCD_DISPLAY("Card unknown");
            }
        } else {
            //idle mode
            LCD_DISPLAY("Card read");
            String idInputCapture = idInput;
            authorize(idInput.c_str(), [this, idInputCapture] (JsonObject payload) {
                if (idTagIsAccepted(payload)) {
                    beginTransaction(idInputCapture.c_str());
                    LCD_DISPLAY("Card accepted");
                } else {
                    LCD_DISPLAY("Card unknown");
                }
            }, nullptr, [this, idInputCapture] () {
                if (*allowOfflineTxForUnknownId) {
                    LCD_DISPLAY("Offline mode");
                    beginTransaction(idInputCapture.c_str());
                } else {
                    LCD_DISPLAY("OCPP timeout");
                }
            });
        }

        return true;
    };

    rfid->setOnCardScanned(&onIdTagInput);

    setOnResetExecute([this] (bool resetHard) {
        if (resetHard) {
            //TODO send reset command to all peripherals
            //see https://github.com/OpenEVSE/ESP32_WiFi_V4.x/issues/228
        }

        restart_system();
    });

    setOnUnlockConnectorInOut([] () {
        //TODO Send unlock command to peripherals. If successful, return true, otherwise false
        //see https://github.com/OpenEVSE/ESP32_WiFi_V4.x/issues/230
        return false;
    });

    setOnSetChargingProfileRequest([this, patchChargingProfileUnit] (JsonObject request) {
        const char *unit = request["csChargingProfiles"]["chargingSchedule"]["chargingRateUnit"] | "W";
        if (unit && (unit[0] == 'A' || unit[0] == 'a')) {
            *patchChargingProfileUnit = "A";
            DBUGLN("[ocpp] ChargingRateUnit from now on A");
        } else {
            *patchChargingProfileUnit = "W";
            DBUGLN("[ocpp] ChargingRateUnit from now on W");
        }
    });
}

unsigned long ArduinoOcppTask::loop(MicroTasks::WakeReason reason) {

    if (arduinoOcppInitialized) {
        OCPP_loop();
    }

    if (arduinoOcppInitialized) {
        
        /*
         * Generate messages for LCD
         */

        if (evse->isVehicleConnected() && !vehicleConnected) {
            //vehicle plugged
            if (!getTransactionIdTag()) {
                //vehicle plugged before authorization
                
                if (config_rfid_enabled()) {
                    LCD_DISPLAY("Need card");
                } else if (!config_ocpp_auto_authorization()) {
                    //wait for RemoteStartTransaction
                    LCD_DISPLAY("Need authorization");
                }
                //if auto-authorize is on, transaction starts without further user interaction
            }
        }
        vehicleConnected = evse->isVehicleConnected();

        if (ocppSessionDisplay && !getTransactionIdTag()) {
            //Session unauthorized. Show if StartTransaction didn't succeed
            if (ocppTxIdDisplay < 0) {
                if (config_rfid_enabled()) {
                    LCD_DISPLAY("Card timeout");
                    LCD_DISPLAY("Present card again");
                } else {
                    LCD_DISPLAY("Auth timeout");
                }
            }
        } else if (!ocppSessionDisplay && getTransactionIdTag()) {
            //Session recently authorized
            if (!evse->isVehicleConnected()) {
                LCD_DISPLAY("Plug in cable");
            }
        }
        ocppSessionDisplay = getTransactionIdTag();

        if (ocppTxIdDisplay < 0 && getTransactionId() >= 0) { //tx started
            LCD_DISPLAY("OCPP start tx");
        }
        if (ocppTxIdDisplay <= 0 && getTransactionId() > 0) { //txId assigned
            String txIdMsg = "TxID ";
            txIdMsg += String(getTransactionId());
            LCD_DISPLAY(txIdMsg);
        }
        if (ocppTxIdDisplay >= 0 && getTransactionId() < 0) { //tx stopped
            LCD_DISPLAY("OCPP stop tx");
        }
        if (ocppTxIdDisplay > 0 && getTransactionId() < 0) { //stopped Tx had txId (not offline-only)
            String txIdMsg = "TxID ";
            txIdMsg += String(ocppTxIdDisplay);
            txIdMsg += " end";
            LCD_DISPLAY(txIdMsg);
        }
        ocppTxIdDisplay = getTransactionId();

        /*
         * Synchronize OCPP config updates with OpenEVSE
         */

        uint16_t configRev = freevendActive->getValueRevision() +
                             freevendIdTag->getValueRevision() +
                             allowOfflineTxForUnknownId->getValueRevision();
        
        if (configRev != trackConfigRevision) {
            DynamicJsonDocument updateQuery (JSON_OBJECT_SIZE(3)); //use JSON in no-copy mode
            updateQuery["ocpp_auth_auto"] = *freevendActive ? 1 : 0;
            updateQuery["ocpp_idtag"] = (const char*) *freevendIdTag;
            updateQuery["ocpp_auth_offline"] = *allowOfflineTxForUnknownId ? 1 : 0;
            config_deserialize(updateQuery);
            config_commit();

            trackConfigRevision = configRev;
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

    if (!arduinoOcppInitialized || !config_ocpp_enabled()) {
        if (evse->clientHasClaim(EvseClient_OpenEVSE_OCPP)) {
            evse->release(EvseClient_OpenEVSE_OCPP);
        }
        return;
    }

    if (ocppPermitsCharge()) {
        evseState = EvseState::Active;
    } else {
        evseState = EvseState::Disabled;
    }

    evseProperties = evseState;

    //OCPP Smart Charging?
    if (charging_limit < 0.f) {
        //OCPP Smart Charging is off. Nothing to do
    } else if (charging_limit < evse->getMinCurrent()) {
        //allowed charge rate is "equal or almost equal" to 0W
        evseState = EvseState::Disabled; //override state
        evseProperties = evseState; //renew properties
    } else {
        //charge rate is valid. Set charge rate
        evseProperties.setChargeCurrent(charging_limit);
    }

    if (!bootNotificationAccepted) {
        evseState = EvseState::Disabled; //override state
        evseProperties = evseState; //renew properties
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
    if (evseState == EvseState::None) {
        //the claiming rules don't specify the EVSE state

        //release claim if still set
        if (evse->clientHasClaim(EvseClient_OpenEVSE_OCPP)) {
            evse->release(EvseClient_OpenEVSE_OCPP);
        }
    } else {
        //the claiming rules specify that the EVSE is either active or inactive

        //set claim if updated
        if (!evse->clientHasClaim(EvseClient_OpenEVSE_OCPP) ||
                    evse->getState(EvseClient_OpenEVSE_OCPP) != evseProperties.getState() ||
                    evse->getChargeCurrent(EvseClient_OpenEVSE_OCPP) != evseProperties.getChargeCurrent()) {
            
            evse->claim(EvseClient_OpenEVSE_OCPP, EvseManager_Priority_OCPP, evseProperties);
        }
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

                eventLog->enumerate(index, [this, startTime, stopTime, &body, SUFFIX_RESERVED_AREA, &firstEntry, &overflow] (String time, EventType type, const String &logEntry, EvseState managerState, uint8_t evseState, uint32_t evseFlags, uint32_t pilot, double energy, uint32_t elapsed, double temperature, double temperatureMax, uint8_t divertMode, uint8_t shaper) {
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

                    restart_system();
                }, [this] (int error_code) {
                    //onFailure
                    updateFailure = true;
                });
        });
    }
}

bool ArduinoOcppTask::isConnected() {
    if (instance && instance->ocppSocket) {
        return instance->ocppSocket->isConnectionOpen();
    }
    return false;
}

bool ArduinoOcppTask::idTagIsAccepted(JsonObject payload) {
    const char *status = payload["idTagInfo"]["status"] | "Invalid";
    return !strcmp(status, "Accepted");
}
