/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#include "ocpp.h"

#include <MicroOcpp.h>
#include <MicroOcpp/Model/Diagnostics/DiagnosticsService.h>
#include <MicroOcpp/Model/FirmwareManagement/FirmwareService.h>
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

/*
 * adapter of OpenEVSE configs so that the OCPP library can work with them
 */
class OcppConfigAdapter : public MicroOcpp::Configuration {
private:
    OcppTask& ocppTask;
    const char *keyOcpp = nullptr;
    const char *keyOpenEvse = nullptr;
    bool (*configGetBoolCb)() = nullptr;
    String *configString = nullptr;
    MicroOcpp::TConfig type;

    OcppConfigAdapter(OcppTask& ocppTask, const char *keyOcpp, const char *keyOpenEvse, MicroOcpp::TConfig type)
            : ocppTask(ocppTask), keyOcpp(keyOcpp), keyOpenEvse(keyOpenEvse), type(type) {

    }

public:

    static std::unique_ptr<OcppConfigAdapter> makeConfigBool(OcppTask& ocppTask, const char *keyOcpp, const char *keyOpenEvse, bool (*configGetBoolCb)()) {
        auto res = std::unique_ptr<OcppConfigAdapter>(new OcppConfigAdapter(ocppTask, keyOcpp, keyOpenEvse, MicroOcpp::TConfig::Bool));
        res->configGetBoolCb = configGetBoolCb;
        return res;
    }

    static std::unique_ptr<OcppConfigAdapter> makeConfigString(OcppTask& ocppTask, const char *keyOcpp, const char *keyOpenEvse, String& value) {
        auto res = std::unique_ptr<OcppConfigAdapter>(new OcppConfigAdapter(ocppTask, keyOcpp, keyOpenEvse, MicroOcpp::TConfig::String));
        res->configString = &value;
        return res;
    }

    bool setKey(const char *key) override {
        keyOcpp = key;
        return true;
    }

    const char *getKey() override {
        return keyOcpp;
    }

    void setBool(bool v) override { //OCPP lib calls this to change config
        ocppTask.setSynchronizationLock(true); //avoid that `reconfigure()` will be called
        config_set(keyOpenEvse, (uint32_t) (v ? 1 : 0));
        config_commit();
        ocppTask.setSynchronizationLock(false);
    }

    bool setString(const char *v) override { //OCPP lib calls this to change config
        ocppTask.setSynchronizationLock(true); //avoid that `reconfigure()` will be called
        config_set(keyOpenEvse, (String) (v ? v : ""));
        config_commit();
        ocppTask.setSynchronizationLock(false);
        return true;
    }

    bool getBool() override {
        return configGetBoolCb ? configGetBoolCb() : false;
    }

    const char *getString() override { //always returns c-string (empty if undefined)
        return configString && configString->c_str() ? configString->c_str() : "";
    }

    MicroOcpp::TConfig getType() override {
        return type;
    }
};

/*
 * Implementation of the OCPP task
 */

OcppTask *OcppTask::instance = nullptr;

void dbug_wrapper(const char *msg) {
    DBUG(msg);
}

OcppTask::OcppTask() : MicroTasks::Task() {

}

OcppTask::~OcppTask() {
    if (connection != nullptr) delete connection;
    instance = nullptr;
}

void OcppTask::begin(EvseManager &evse, LcdTask &lcd, EventLog &eventLog, RfidTask &rfid) {

    this->evse = &evse;
    this->lcd = &lcd;
    this->eventLog = &eventLog;
    this->rfid = &rfid;

    mocpp_set_console_out(dbug_wrapper);

    instance = this; //cannot be in constructer because object is invalid before .begin()

    MicroTask.startTask(this);

    reconfigure();
}

void OcppTask::notifyConfigChanged() {
    if (instance && !instance->synchronizationLock) {
        instance->reconfigure();
    }
}

void OcppTask::reconfigure() {

    if (config_ocpp_enabled()) {
        //OCPP enabled via OpenEVSE config. Load library (if not done yet) and apply OpenEVSE configs

        if (!getOcppContext()) {
            //first time execution, library not initialized yet
            initializeMicroOcpp();
        }

        if (!ocpp_server.equals(connection->getBackendUrl()) ||
                !ocpp_chargeBoxId.equals(connection->getChargeBoxId()) ||
                !ocpp_authkey.equals(connection->getAuthKey())) {
            //OpenEVSE WS URL configs have been updated - these must be applied manually
            connection->reloadConfigs();
        }
    } else {
        //OCPP disabled via OpenEVSE config

        if (getOcppContext()) {
            //library still running. Deinitialize
            deinitializeMicroOcpp();
        }
    }

    MicroTask.wakeTask(this);
}

void OcppTask::initializeMicroOcpp() {

    auto filesystem = MicroOcpp::makeDefaultFilesystemAdapter(MicroOcpp::FilesystemOpt::Use);

    /*
     * Clean local OCPP files when upgrading to MicroOcpp v1.0. Unfortunately, config changes made by
     * the OCPP server will be lost. The WebSocket URL is stored in the OpenEVSE configs is not affected
     */
    MicroOcpp::configuration_init(filesystem);
    auto configVersion = MicroOcpp::declareConfiguration<const char*>("MicroOcppVersion", "0.x", MO_KEYVALUE_FN, false, false, false);
    MicroOcpp::configuration_load(MO_KEYVALUE_FN);
    if (configVersion && !strcmp(configVersion->getString(), "0.x")) {
        if (auto root = LittleFS.open("/")) {
            while (auto file = root.openNextFile()) {
                if (!strcmp(file.name(), "arduino-ocpp.cnf") ||
                        !strcmp(file.name(), "ws-conn.jsn") ||
                        !strncmp(file.name(), "sd", strlen("sd")) ||
                        !strncmp(file.name(), "tx", strlen("tx")) ||
                        !strncmp(file.name(), "op", strlen("op")) ||
                        !strncmp(file.name(), "ocpp-", strlen("ocpp-")) ||
                        !strncmp(file.name(), "client-state", strlen("client-state"))) {
                    std::string path = file.path();
                    file.close();
                    auto success = LittleFS.remove(path.c_str());
                    DBUGF("[ocpp] migration: remove file %s %s", path.c_str(), success ? "" : "failure");
                }
            }
        }
        DBUGF("[ocpp] migration done");
    }
    MicroOcpp::configuration_deinit(); //reinit to become fully effective

    /*
     * Create mapping of OCPP configs to OpenEVSE configs using config adapters.
     *
     * Add config adapters to a config container
     */
    MicroOcpp::configuration_init(filesystem);

    std::shared_ptr<MicroOcpp::ConfigurationContainerVolatile> openEvseConfigs =
        MicroOcpp::makeConfigurationContainerVolatile(
            CONFIGURATION_VOLATILE "/openevse", //container ID
            true);                              //configs are visible to OCPP server

    openEvseConfigs->add(OcppConfigAdapter::makeConfigString(*this,
            MO_CONFIG_EXT_PREFIX "BackendUrl",                        //config key in OCPP lib
            "ocpp_server",                                            //config key in OpenEVSE configs
            ocpp_server));                                            //reference to OpenEVSE config value
    openEvseConfigs->add(OcppConfigAdapter::makeConfigString(*this,
            MO_CONFIG_EXT_PREFIX "ChargeBoxId",
            "ocpp_chargeBoxId",
            ocpp_chargeBoxId));
    openEvseConfigs->add(OcppConfigAdapter::makeConfigString(*this,
            "AuthorizationKey",
            "ocpp_authkey",
            ocpp_authkey));
    openEvseConfigs->add(OcppConfigAdapter::makeConfigBool(*this,
            MO_CONFIG_EXT_PREFIX "FreeVendActive",
            "ocpp_auth_auto",
            config_ocpp_auto_authorization));                         //config value getter callback
    openEvseConfigs->add(OcppConfigAdapter::makeConfigString(*this,
            MO_CONFIG_EXT_PREFIX "FreeVendIdTag",
            "ocpp_idtag",
            ocpp_idtag));
    openEvseConfigs->add(OcppConfigAdapter::makeConfigBool(*this,
            "AllowOfflineTxForUnknownId",
            "ocpp_auth_offline",
            config_ocpp_offline_authorization));

    MicroOcpp::addConfigurationContainer(openEvseConfigs);

    /*
     * Set OCPP-only factory defaults
     */
    MicroOcpp::declareConfiguration<const char*>(
        "MeterValuesSampledData", "Power.Active.Import,Energy.Active.Import.Register,Current.Import,Current.Offered,Voltage,Temperature"); //read all sensors by default
    MicroOcpp::declareConfiguration<bool>(
        MO_CONFIG_EXT_PREFIX "PreBootTransactions", true); //allow transactions before the OCPP connection has been established (can lead to data loss)
    MicroOcpp::declareConfiguration<bool>(
        MO_CONFIG_EXT_PREFIX "SilentOfflineTransactions", true); //disable transaction journaling when being offline for a long time (can lead to data loss)

    connection = new MicroOcpp::MOcppMongooseClient(Mongoose.getMgr(), nullptr, nullptr, nullptr, nullptr, filesystem);

    /*
     * Initialize the OCPP library and provide it with the charger credentials
     */
    mocpp_initialize(*connection, ChargerCredentials(
            "Advanced Series",         //chargePointModel
            "OpenEVSE",                //chargePointVendor
            currentfirmware.c_str(),   //firmwareVersion
            serial.c_str(),            //chargePointSerialNumber
            evse->getFirmwareVersion() //meterSerialNumber
        ), filesystem,
        true); //enable auto recovery

    loadEvseBehavior();
    initializeDiagnosticsService();
    initializeFwService();
}

void OcppTask::deinitializeMicroOcpp() {
    rfid->setOnCardScanned(nullptr);
    mocpp_deinitialize();
    delete connection;
    connection = nullptr;
}

void OcppTask::setup() {

}

void OcppTask::loadEvseBehavior() {

    /*
     * Synchronize OpenEVSE data with OCPP-library data
     */

    //basic meter readings

    setEnergyMeterInput([this] () {
        return evse->getTotalEnergy() * 1000.; //convert kWh into Wh
    });

    setPowerMeterInput([this] () {
        return evse->getAmps() * evse->getVoltage();
    });

    //further meter readings

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

    addErrorDataInput([this] () -> MicroOcpp::ErrorData {
        if (evse->getEvseState() == OPENEVSE_STATE_DIODE_CHECK_FAILED ||
                evse->getEvseState() == OPENEVSE_STATE_GFI_FAULT ||
                evse->getEvseState() == OPENEVSE_STATE_NO_EARTH_GROUND ||
                evse->getEvseState() == OPENEVSE_STATE_GFI_SELF_TEST_FAILED) {

            MicroOcpp::ErrorData error = "GroundFailure";

            //add free text error info
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
    setTxNotificationOutput([this] (MicroOcpp::Transaction*, MicroOcpp::TxNotification notification) {
        switch (notification) {
            case MicroOcpp::TxNotification::AuthorizationRejected:
                LCD_DISPLAY("Card unknown");
                break;
            case MicroOcpp::TxNotification::AuthorizationTimeout:
                LCD_DISPLAY("Server timeout");
                break;
            case MicroOcpp::TxNotification::Authorized:
                LCD_DISPLAY("Card accepted");
                break;
            case MicroOcpp::TxNotification::ConnectionTimeout:
                LCD_DISPLAY("Aborted / no EV");
                break;
            case MicroOcpp::TxNotification::DeAuthorized:
                LCD_DISPLAY("Card unknown");
                break;
            case MicroOcpp::TxNotification::RemoteStart:
                if (!evse->isVehicleConnected()) {
                    LCD_DISPLAY("Plug in cable");
                }
                break;
            case MicroOcpp::TxNotification::ReservationConflict:
                LCD_DISPLAY("EVSE reserved");
                break;
            case MicroOcpp::TxNotification::StartTx:
                LCD_DISPLAY("Tx started");
                break;
            case MicroOcpp::TxNotification::StopTx:
                LCD_DISPLAY("Tx stopped");
                break;
            default:
                break;
        }
    });
}

unsigned long OcppTask::loop(MicroTasks::WakeReason reason) {

    if (getOcppContext()) {
        //MicroOcpp is initialized

        mocpp_loop();

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

void OcppTask::updateEvseClaim() {

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

    //check further error condition not handled by Atmega
    if (rfid->communicationFails()) {
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

void OcppTask::initializeDiagnosticsService() {
    MicroOcpp::DiagnosticsService *diagService = getDiagnosticsService();
    if (diagService) {
        diagService->setOnUploadStatusInput([this] () {
            if (diagFailure) {
                return MicroOcpp::UploadStatus::UploadFailed;
            } else if (diagSuccess) {
                return MicroOcpp::UploadStatus::Uploaded;
            } else {
                return MicroOcpp::UploadStatus::NotUploaded;
            }
        });

        diagService->setOnUpload([this] (const std::string &location, MicroOcpp::Timestamp &startTime, MicroOcpp::Timestamp &stopTime) {

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
                    MicroOcpp::Timestamp timestamp = MicroOcpp::Timestamp();
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

void OcppTask::initializeFwService() {
    MicroOcpp::FirmwareService *fwService = getFirmwareService();
    if (fwService) {

        fwService->setInstallationStatusInput([this] () {
            if (updateFailure) {
                return MicroOcpp::InstallationStatus::InstallationFailed;
            } else if (updateSuccess) {
                return MicroOcpp::InstallationStatus::Installed;
            } else {
                return MicroOcpp::InstallationStatus::NotInstalled;
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

bool OcppTask::isConnected() {
    if (instance && instance->connection) {
        return instance->connection->isConnected();
    }
    return false;
}

void OcppTask::setSynchronizationLock(bool locked) {
    synchronizationLock = locked;
}
