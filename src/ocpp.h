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

#include <AOcppMongooseClient.h>
#include <MongooseHttpClient.h>

#include <ArduinoOcpp/Core/Configuration.h>

class ArduinoOcppTask: public MicroTasks::Task {
private:
    ArduinoOcpp::AOcppMongooseClient *ocppSocket = NULL;
    EvseManager *evse;
    LcdTask *lcd;
    EventLog *eventLog;
    RfidTask *rfid;

    float charging_limit = -1.f; //in Watts. chargingLimit < 0 means that there is no Smart Charging (and no restrictions )
    int ocppTxIdDisplay {-1};
    bool ocppSessionDisplay {false};

    bool vehicleConnected = false;

    std::function<bool(const String& idTag)> onIdTagInput {nullptr};

    bool resetTriggered = false;
    bool resetHard = false; //default to soft reset
    ulong resetTime;

    MongooseHttpClient diagClient = MongooseHttpClient();
    bool diagSuccess = false, diagFailure = false;
    void initializeDiagnosticsService();

    bool updateSuccess = false, updateFailure = false;
    void initializeFwService();

    void initializeArduinoOcpp();
    bool arduinoOcppInitialized = false;
    void loadEvseBehavior();

    ulong updateEvseClaimLast {0};

    String getCentralSystemUrl();

    static ArduinoOcppTask *instance;

    std::shared_ptr<ArduinoOcpp::Configuration<const char*>> OE_FreeVendActive; //if to autostart transaction when vehicle plugged
    std::shared_ptr<ArduinoOcpp::Configuration<const char*>> OE_FreeVendIdTag;  //idTag for autostart transaction

    //helper functions
    static bool idTagIsAccepted(JsonObject payload);
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
