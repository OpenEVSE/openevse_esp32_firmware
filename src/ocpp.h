/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#ifndef OCPP_H
#define OCPP_H

#include <MicroTasks.h>

#include "evse_man.h"
#include "lcd.h"
#include "rfid.h"

#include <ArduinoOcppMongooseClient.h>
#include <MongooseHttpClient.h>

#include <ArduinoOcpp/Core/Configuration.h>

class ArduinoOcppTask: public MicroTasks::Task {
private:
    ArduinoOcpp::AOcppMongooseClient *ocppSocket = NULL;
    EvseManager *evse;
    LcdTask *lcd;
    EventLog *eventLog;
    RfidTask *rfid;

    float charging_limit = -1.f; //in Amps. charging_limit < 0 means that no charging limit is defined
    bool trackOcppConnected = false;
    bool trackVehicleConnected = false;

    std::function<bool(const String& idTag)> onIdTagInput {nullptr};

    MongooseHttpClient diagClient = MongooseHttpClient();
    bool diagSuccess = false, diagFailure = false;
    void initializeDiagnosticsService();

    bool updateSuccess = false, updateFailure = false;
    void initializeFwService();

    void initializeArduinoOcpp();
    void deinitializeArduinoOcpp();
    void loadEvseBehavior();

    ulong updateEvseClaimLast {0};

    static ArduinoOcppTask *instance;
    std::shared_ptr<ArduinoOcpp::Configuration<const char*>> backendUrl;
    std::shared_ptr<ArduinoOcpp::Configuration<const char*>> chargeBoxId;
    std::shared_ptr<ArduinoOcpp::Configuration<const char*>> authKey;
    std::shared_ptr<ArduinoOcpp::Configuration<bool>> freevendActive; //Authorize automatically
    std::shared_ptr<ArduinoOcpp::Configuration<const char*>> freevendIdTag; //idTag for auto-authorization
    std::shared_ptr<ArduinoOcpp::Configuration<bool>> allowOfflineTxForUnknownId; //temporarily accept all NFC-cards while offline
    std::shared_ptr<ArduinoOcpp::Configuration<bool>> silentOfflineTx; //stop transaction journaling in long offline periods

protected:

    //hook method of MicroTask::Task
    void setup();

    //hook method of MicroTask::Task
    unsigned long loop(MicroTasks::WakeReason reason);

public:
    ArduinoOcppTask();
    ~ArduinoOcppTask();

    void begin(EvseManager &evse, LcdTask &lcd, EventLog &eventLog, RfidTask &rfid);
    
    void updateEvseClaim();

    static void notifyConfigChanged();
    void reconfigure();

    static bool isConnected();
};

#endif
