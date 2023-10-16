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

#include <MicroOcppMongooseClient.h>
#include <MongooseHttpClient.h>

#include <MicroOcpp/Core/Configuration.h>

class OcppTask: public MicroTasks::Task {
private:
    MicroOcpp::MOcppMongooseClient *connection = nullptr;
    EvseManager *evse;
    LcdTask *lcd;
    EventLog *eventLog;
    RfidTask *rfid;

    float charging_limit = -1.f; //in Amps. charging_limit < 0 means that no charging limit is defined
    bool trackOcppConnected = false;
    bool trackVehicleConnected = false;

    std::function<bool(const String& idTag)> onIdTagInput;

    MongooseHttpClient diagClient = MongooseHttpClient();
    bool diagSuccess = false, diagFailure = false;
    void initializeDiagnosticsService();

    bool updateSuccess = false, updateFailure = false;
    void initializeFwService();

    void initializeMicroOcpp();
    void deinitializeMicroOcpp();
    void loadEvseBehavior();

    static OcppTask *instance;

    bool synchronizationLock = false;
protected:

    //hook method of MicroTask::Task
    void setup();

    //hook method of MicroTask::Task
    unsigned long loop(MicroTasks::WakeReason reason);

public:
    OcppTask();
    ~OcppTask();

    void begin(EvseManager &evse, LcdTask &lcd, EventLog &eventLog, RfidTask &rfid);
    
    void updateEvseClaim();

    static void notifyConfigChanged();
    void reconfigure();

    static bool isConnected();

    void setSynchronizationLock(bool locked);
};

#endif
