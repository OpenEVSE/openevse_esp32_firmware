/*
 * Author: Matthias Akstaller
 * Created: 2021-04-09
 */

#ifndef OCPP_H
#define OCPP_H

#include <MicroTasks.h>

#include "evse_man.h"
#include "lcd.h"

#include "MongooseOcppSocketClient.h"
#include <MongooseHttpClient.h>

#define IDTAG_TIMEOUT 30000UL

class ArduinoOcppTask: public MicroTasks::Task {
private:
    MongooseOcppSocketClient *ocppSocket = NULL;
    EvseManager *evse;
    LcdTask *lcd;
    EventLog *eventLog;

    /*
     * OCPP state
     */
    float charging_limit = -1.f; //in Watts. chargingLimit < 0 means that there is no Smart Charging (and no restrictions )

    /*
     * SAE J1772 state
     */
    bool vehicleConnected = false;
    bool vehicleCharging = false;

    std::function<void()> onVehicleConnect = [] () {};
    std::function<void()> onVehicleDisconnect = [] () {};
    std::function<bool(const String& idTag)> onIdTagInput {nullptr};

    String idTag {'\0'};
    ulong idTag_timestamp = 0;
    bool idTag_accepted = false;

    bool resetTriggered = false;
    bool resetHard = false; //default to soft reset
    ulong resetTime;

    MongooseHttpClient diagClient = MongooseHttpClient();
    bool diagSuccess, diagFailure = false;
    void initializeDiagnosticsService();

    bool updateSuccess, updateFailure = false;
    void initializeFwService();

    void initializeArduinoOcpp();
    bool arduinoOcppInitialized = false;
    void loadEvseBehavior();

    String getCentralSystemUrl();

    static ArduinoOcppTask *instance;

    //helper functions
    static bool operationIsAccepted(JsonObject payload);
    static bool idTagIsAccepted(JsonObject payload);
    static bool idTagIsRejected(JsonObject payload);
protected:

    //hook method of MicroTask::Task
    void setup();

    //hook method of MicroTask::Task
    unsigned long loop(MicroTasks::WakeReason reason);

public:
    ArduinoOcppTask();
    ~ArduinoOcppTask();

    void begin(EvseManager &evse, LcdTask &lcd, EventLog &eventLog);
    
    void updateEvseClaim();

    static void notifyConfigChanged();
    void reconfigure();

    std::function<bool(const String& idTag)> *getOnIdTagInput();
};

#endif
