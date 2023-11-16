/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#include "ocpp.h"

#include <ArduinoOcpp.h>
#include <MongooseCore.h>

#include "app_config.h"
#include "http_update.h"
#include "emonesp.h"
#include "certificates.h"

// Time between loop polls
#ifndef OCPP_LOOP_TIME
#define OCPP_LOOP_TIME 200
#endif

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

    MicroTask.startTask(this);

    reconfigure();

    instance = this; //OcppTask is valid now
}

void ArduinoOcppTask::reconfigure() {

    if (config_ocpp_enabled()) {
        //OCPP enabled via OpenEVSE config. Load library (if not done yet) and apply OpenEVSE configs

        if (!getOcppContext()) {
            //first time execution, library not initialized yet
            initializeArduinoOcpp();
        }

        //apply new backend credentials if they have been updated via OpenEVSE GUI. Don't apply
        //if the OCPP server updated them, because this closes the WS connection and can lead
        //to the loss of the OCPP response
        if (!ocpp_server.equals((const char*) *backendUrl)) {
            ocppSocket->setBackendUrl(ocpp_server.c_str());
        }
        if (!ocpp_chargeBoxId.equals((const char*) *chargeBoxId)) {
            ocppSocket->setChargeBoxId(ocpp_chargeBoxId.c_str());
        }
        if (!ocpp_authkey.equals((const char*) *authKey)) {
            ocppSocket->setAuthKey(ocpp_authkey.c_str());
        }

        //apply further configs each time. They don't have potentially negative side effects
        *freevendActive = config_ocpp_auto_authorization();
        *freevendIdTag = ocpp_idtag.c_str();
        *allowOfflineTxForUnknownId = config_ocpp_offline_authorization();
        if (config_ocpp_auto_authorization()) {
            *silentOfflineTx = true; //recommended to disable transaction journaling when being offline in Freevend mode
        }

        ArduinoOcpp::configuration_save();
    } else {
        //OCPP disabled via OpenEVSE config

        if (getOcppContext()) {
            //library still running. Deinitialize
            deinitializeArduinoOcpp();
        }
    }

    MicroTask.wakeTask(this);
}

void ArduinoOcppTask::initializeArduinoOcpp() {

    auto filesystem = ArduinoOcpp::makeDefaultFilesystemAdapter(ArduinoOcpp::FilesystemOpt::Use);

    ocppSocket = new ArduinoOcpp::AOcppMongooseClient(Mongoose.getMgr(), nullptr, nullptr, nullptr, nullptr, filesystem);

    /*
     * Set OCPP-only factory defaults
     */
    ArduinoOcpp::configuration_init(filesystem);
    ArduinoOcpp::declareConfiguration<const char*>(
        "MeterValuesSampledData", "Power.Active.Import,Energy.Active.Import.Register,Current.Import,Current.Offered,Voltage,Temperature");
    ArduinoOcpp::declareConfiguration<bool>(
        "AO_PreBootTransactions", true);

    /*
     * Initialize the OCPP library and provide it with the charger credentials
     */
    ocpp_initialize(*ocppSocket, ChargerCredentials(
            "Advanced Series",         //chargePointModel
            "OpenEVSE",                //chargePointVendor
            currentfirmware.c_str(),   //firmwareVersion
            serial.c_str(),            //chargePointSerialNumber
            evse->getFirmwareVersion() //meterSerialNumber
        ), ArduinoOcpp::FilesystemOpt::Use);

    /*
     * Load OCPP configs. Default values will be overwritten by OpenEVSE configs. Mark configs
     * to require reboot if changed via OCPP server
     */
    backendUrl = ArduinoOcpp::declareConfiguration<const char*>("AO_BackendUrl", "", AO_FILENAME_PREFIX "ocpp-creds.jsn", true, true, true, true);
    chargeBoxId = ArduinoOcpp::declareConfiguration<const char*>("AO_ChargeBoxId", "", AO_FILENAME_PREFIX "ocpp-creds.jsn", true, true, true, true);
    authKey = ArduinoOcpp::declareConfiguration<const char*>("AuthorizationKey", "", AO_FILENAME_PREFIX "ocpp-creds.jsn", true, true, true, true);
    freevendActive = ArduinoOcpp::declareConfiguration<bool>("AO_FreeVendActive", true, CONFIGURATION_FN, true, true, true, true);
    freevendIdTag = ArduinoOcpp::declareConfiguration<const char*>("AO_FreeVendIdTag", "DefaultIdTag", CONFIGURATION_FN, true, true, true, true);
    allowOfflineTxForUnknownId = ArduinoOcpp::declareConfiguration<bool>("AllowOfflineTxForUnknownId", true, CONFIGURATION_FN, true, true, true, true);
    silentOfflineTx = ArduinoOcpp::declareConfiguration<bool>("AO_SilentOfflineTransactions", true, CONFIGURATION_FN, true, true, true, true);

    //when the OCPP server updates the configs, the following callback will apply them to the OpenEVSE configs
    setOnReceiveRequest("ChangeConfiguration", [this] (JsonObject) {
        config_set("ocpp_server", String((const char*) *backendUrl));
        config_set("ocpp_chargeBoxId", String((const char*) *chargeBoxId));
        config_set("ocpp_authkey", String((const char*) *authKey));
        config_set("ocpp_auth_auto", (uint32_t) (*freevendActive ? 1 : 0));
        config_set("ocpp_idtag", String((const char*) *freevendIdTag));
        config_set("ocpp_auth_offline", (uint32_t) (*allowOfflineTxForUnknownId ? 1 : 0));
        config_commit();
    });

    loadEvseBehavior();
    initializeDiagnosticsService();
    initializeFwService();
}

void ArduinoOcppTask::deinitializeArduinoOcpp() {
    backendUrl.reset();
    chargeBoxId.reset();
    authKey.reset();
    freevendActive.reset();
    freevendIdTag.reset();
    allowOfflineTxForUnknownId.reset();
    silentOfflineTx.reset();
    rfid->setOnCardScanned(nullptr);
    ocpp_deinitialize();
    delete ocppSocket;
    ocppSocket = nullptr;
}

void ArduinoOcppTask::setup() {

}

void ArduinoOcppTask::loadEvseBehavior() {

    /*
     * Synchronize OpenEVSE data with OCPP-library data
     */

    addMeterValueInput([this] () {
            return evse->getAmps() * evse->getVoltage();
        },
        "Power.Active.Import",
        "W");

    addMeterValueInput([this] () {
            return evse->getTotalEnergy() * 1000.; //convert kWh into Wh
        },
        "Energy.Active.Import.Register",
        "Wh");

    addMeterValueInput([this] () {
            return evse->getAmps();
        },
        "Current.Import",
        "A");

    addMeterValueInput([this] () {
            return (float) evse->getChargeCurrent();
        },
        "Current.Offered",
        "A");

    addMeterValueInput([this] () {
            return evse->getVoltage();
        },
        "Voltage",
        "V");

    addMeterValueInput([this] () {
            return evse->getTemperature(EVSE_MONITOR_TEMP_MONITOR);
        },
        "Temperature",
        "Celsius");

    setSmartChargingOutput([this] (float power, float current, int nphases) {
        if (power >= 0.f && current >= 0.f) {
            //both defined, take smaller value
            charging_limit = std::min(power / VOLTAGE_DEFAULT, current);
        } else if (current >= 0.f) {
            //current defined
            charging_limit = current;
        } else if (power >= 0.f) {
            //power defined
            charging_limit = power / (float) VOLTAGE_DEFAULT;
        } else {
            //Smart charging disabled / limit undefined
            charging_limit = -1.f;
        }
    });

    setConnectorPluggedInput([this] () {
        return evse->isVehicleConnected();
    });

    setEvReadyInput([this] () {
        return evse->isCharging();
    });

    setEvseReadyInput([this] () {
        return evse->isActive();
    });

    /*
     * Report failures to central system. Note that the error codes are standardized in OCPP
     */

    addErrorCodeInput([this] () -> const char* {
        if (evse->getEvseState() == OPENEVSE_STATE_OVER_TEMPERATURE) {
            return "HighTemperature";
        }
        return nullptr;
    });

    addErrorCodeInput([this] () -> const char* {
        if (evse->getEvseState() == OPENEVSE_STATE_OVER_CURRENT) {
            return "OverCurrentFailure";
        }
        return nullptr;
    });

    addErrorCodeInput([this] () -> const char* {
        if (evse->getEvseState() == OPENEVSE_STATE_STUCK_RELAY) {
            return "PowerSwitchFailure";
        }
        return nullptr;
    });

    addErrorCodeInput([this] () -> const char* {
        if (rfid->communicationFails()) {
            return "ReaderFailure";
        }
        return nullptr;
    });

    addErrorDataInput([this] () -> ArduinoOcpp::ErrorData {
        if (evse->getEvseState() == OPENEVSE_STATE_DIODE_CHECK_FAILED ||
                evse->getEvseState() == OPENEVSE_STATE_GFI_FAULT ||
                evse->getEvseState() == OPENEVSE_STATE_NO_EARTH_GROUND ||
                evse->getEvseState() == OPENEVSE_STATE_GFI_SELF_TEST_FAILED) {

            ArduinoOcpp::ErrorData error = "GroundFailure";

            error.info = evse->getEvseState() == OPENEVSE_STATE_DIODE_CHECK_FAILED ? "diode check failed" :
                         evse->getEvseState() == OPENEVSE_STATE_GFI_FAULT ? "GFI fault" :
                         evse->getEvseState() == OPENEVSE_STATE_NO_EARTH_GROUND ? "no earth / ground" :
                         evse->getEvseState() == OPENEVSE_STATE_GFI_SELF_TEST_FAILED ? "GFI self test failed" : nullptr;
            return error;
        }
        return nullptr;
    });

    onIdTagInput = [this] (const String& idInput) {
        if (idInput.isEmpty()) {
            DBUGLN("[ocpp] empty idTag");
            return true;
        }
        if (!isOperative()) {
            LCD_DISPLAY("Out of service");
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
            beginTransaction(idInput.c_str());
        }

        return true;
    };

    rfid->setOnCardScanned(&onIdTagInput);

    setOnResetExecute([this] (bool resetHard) {
        if (resetHard) {
            evse->restartEvse(); //hard reset applies to EVSE module and ESP32
        }

        restart_system();
    });

    /*
     * Give the user feedback about the status of the OCPP transaction
     */
    setTxNotificationOutput([this] (ArduinoOcpp::TxNotification notification, ArduinoOcpp::Transaction*) {
        switch (notification) {
            case ArduinoOcpp::TxNotification::AuthorizationRejected:
                LCD_DISPLAY("Card unkown");
                break;
            case ArduinoOcpp::TxNotification::AuthorizationTimeout:
                LCD_DISPLAY("Server timeout");
                break;
            case ArduinoOcpp::TxNotification::Authorized:
                LCD_DISPLAY("Card accepted");
                break;
            case ArduinoOcpp::TxNotification::ConnectionTimeout:
                LCD_DISPLAY("Aborted / no EV");
                break;
            case ArduinoOcpp::TxNotification::DeAuthorized:
                LCD_DISPLAY("Card unkown");
                break;
            case ArduinoOcpp::TxNotification::RemoteStart:
                if (!evse->isVehicleConnected()) {
                    LCD_DISPLAY("Plug in cable");
                }
                break;
            case ArduinoOcpp::TxNotification::ReservationConflict:
                LCD_DISPLAY("EVSE reserved");
                break;
            case ArduinoOcpp::TxNotification::StartTx:
                LCD_DISPLAY("Tx started");
                break;
            case ArduinoOcpp::TxNotification::StopTx:
                LCD_DISPLAY("Tx stopped");
                break;
            default:
                break;
        }
    });
}

unsigned long ArduinoOcppTask::loop(MicroTasks::WakeReason reason) {

    if (getOcppContext()) {
        //ArduinoOcpp is initialized

        ocpp_loop();

        /*
         * Generate messages for LCD
         */

        if (evse->isVehicleConnected() && !trackVehicleConnected) {
            //vehicle plugged
            if (!isOperative()) {
                LCD_DISPLAY("No OCPP service");
            } else if (!isTransactionActive()) {
                //vehicle plugged before authorization

                if (config_rfid_enabled()) {
                    LCD_DISPLAY("Need card");
                } else if (!config_ocpp_auto_authorization()) {
                    //wait for RemoteStartTransaction
                    LCD_DISPLAY("Wait for app");
                }
                //if auto-authorize is on, transaction starts without further user interaction
            }
        }
        trackVehicleConnected = evse->isVehicleConnected();

        if (isConnected() && !trackOcppConnected) {
            LCD_DISPLAY("OCPP connected");
        }
        trackOcppConnected = isConnected();
    }

    updateEvseClaim();

    return config_ocpp_enabled() ? OCPP_LOOP_TIME : MicroTask.Infinate;
}

void ArduinoOcppTask::updateEvseClaim() {

    EvseState evseState;
    EvseProperties evseProperties;

    if (!getOcppContext() || !config_ocpp_enabled()) {
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
        diagService->setOnUploadStatusInput([this] () {
            if (diagFailure) {
                return ArduinoOcpp::UploadStatus::UploadFailed;
            } else if (diagSuccess) {
                return ArduinoOcpp::UploadStatus::Uploaded;
            } else {
                return ArduinoOcpp::UploadStatus::NotUploaded;
            }
        });

        diagService->setOnUpload([this] (const std::string &location, ArduinoOcpp::Timestamp &startTime, ArduinoOcpp::Timestamp &stopTime) {

            //reset reported state
            diagSuccess = false;
            diagFailure = false;

            //check if input URL is valid
            unsigned int port_i = 0;
            struct mg_str scheme, query, fragment;
            if (mg_parse_uri(mg_mk_str(location.c_str()), &scheme, NULL, NULL, &port_i, NULL, &query, &fragment)) {
                DBUGF("[ocpp] Diagnostics upload, invalid URL: %s", location.c_str());
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
                    ArduinoOcpp::Timestamp timestamp = ArduinoOcpp::Timestamp();
                    if (time.isEmpty() || !timestamp.setTime(time.c_str())) {
                        DBUGF("[ocpp] Diagnostics upload, cannot parse timestamp format: %s", time.c_str());
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

            DBUGF("[ocpp] POST diagnostics file to %s", location.c_str());

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

        fwService->setInstallationStatusInput([this] () {
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
