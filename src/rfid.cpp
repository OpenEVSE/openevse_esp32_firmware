/*
 * Author: Oliver Norin
 *         Matthias Akstaller
 */

#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_NFCREADER)
#undef ENABLE_DEBUG
#endif

#include "rfid.h"

#include "debug.h"
#include "mqtt.h"
#include "lcd.h"
#include "app_config.h"
#include "input.h"
#include "openevse.h"
#include "event.h"

#define AUTHENTICATION_TIMEOUT  30 * 1000UL
#define RFID_ADD_WAITINGPERIOD  60 * 1000UL

RfidTask::RfidTask() :
  MicroTasks::Task()
{
}

void RfidTask::begin(EvseManager &evse, RfidReader &rfid){
    _evse = &evse;
    _rfid = &rfid;
    _rfid->setOnCardDetected([this] (String &uid) {
        this->scanCard(uid);
    });
    MicroTask.startTask(this);
}

void RfidTask::setup(){

}

void RfidTask::scanCard(String& uid){
    if(waitingForTag){
        waitingForTag = false;
        lcd.display("Tag detected!", 0, 0, 0, LCD_CLEAR_LINE);
        lcd.display(uid, 0, 1, 3000, LCD_CLEAR_LINE);

        StaticJsonDocument<128> event;
        event["rfid_input"] = uid;
        event["rfid_waiting"] = 0;
        event_send(event);

    } else if (onCardScanned && (*onCardScanned) && (*onCardScanned)(uid)){
        //OCPP will process the card
    }else{
        // Check if tag is stored locally
        char storedTags[rfid_storage.length() + 1];
        rfid_storage.toCharArray(storedTags, rfid_storage.length()+1);
        char* storedTag = strtok(storedTags, ",");
        bool foundCard = false;
        while(storedTag)
        {
            String storedTagStr = storedTag;
            storedTagStr.replace(" ", "");
            if(storedTagStr.equals(uid)){
                foundCard = true;
                if (!isAuthenticated()){
                    setAuthentication(uid);
                    lcd.display("RFID: authenticated", 0, 1, 5 * 1000, LCD_CLEAR_LINE);
                    DBUGLN(F("[rfid] found card"));
                } else if (uid == authenticatedTag) {
                    resetAuthentication();
                    lcd.display("RFID: finished. See you next time!", 0, 1, 5 * 1000, LCD_CLEAR_LINE);
                    DBUGLN(F("[rfid] finished by presenting card"));
                } else {
                    lcd.display("RFID: card does not match", 0, 1, 5 * 1000, LCD_CLEAR_LINE);
                    DBUGLN(F("[rfid] card does not match"));
                }
                break;
            }
            storedTag = strtok(NULL, ",");
        }

        if (!foundCard) {
            lcd.display("RFID: did not recognize card", 0, 1, 5 * 1000, LCD_CLEAR_LINE);
            DBUGLN(F("[rfid] did not recognize card"));
        }

        // Send to MQTT broker
        DynamicJsonDocument data{JSON_OBJECT_SIZE(1) + uid.length() + 1};
        data["rfid"] = uid;
        mqtt_publish(data);
    }
}

unsigned long RfidTask::loop(MicroTasks::WakeReason reason){

    if (_evse->isVehicleConnected() && !vehicleConnected) {
        vehicleConnected = _evse->isVehicleConnected();
    }

    if (!_evse->isVehicleConnected() && vehicleConnected) {
        vehicleConnected = _evse->isVehicleConnected();

        if (isAuthenticated()) {
            lcd.display("RFID: finished. See you next time!", 0, 1, 5 * 1000, LCD_CLEAR_LINE);
            DBUGLN(F("[rfid] finished by unplugging"));
        }
        resetAuthentication();
    }

    if (authenticationTimeoutExpired()) {
        resetAuthentication();
        lcd.display("RFID: please present again", 0, 1, 20 * 1000, LCD_CLEAR_LINE);
    }

    updateEvseClaim();

    if (!config_rfid_enabled()) {
        resetAuthentication();
        return 1000;
    }

    if(waitingForTag){
        if (millis() - waitingBegin < RFID_ADD_WAITINGPERIOD) {
            String msg = "tag... ";
            msg += (waitingBegin + RFID_ADD_WAITINGPERIOD - millis()) / 1000;
            msg += "s";
            lcd.display("Waiting for RFID", 0, 0, 0, LCD_CLEAR_LINE);
            lcd.display(msg, 0, 1, 1000, LCD_CLEAR_LINE);
        } else {
            waitingForTag = false;
            StaticJsonDocument<128> event;
            event["rfid_waiting"] = 0;
            event_send(event);
        }
    }

    return 1000;
}

bool RfidTask::authenticationTimeoutExpired(){
    return !authenticatedTag.isEmpty() && millis() - authentication_timestamp > AUTHENTICATION_TIMEOUT && !vehicleConnected;
}

bool RfidTask::isAuthenticated(){
    if (authenticatedTag.isEmpty()) {
        return false;
    } else {
        return vehicleConnected || millis() - authentication_timestamp <= AUTHENTICATION_TIMEOUT;
    }
}

String RfidTask::getAuthenticatedTag(){
    if (isAuthenticated())
        return authenticatedTag;
    else
        return String('\0');
}

void RfidTask::resetAuthentication(){
    authenticatedTag = String('\0');
}

void RfidTask::setAuthentication(String &idTag){
    authenticatedTag = idTag;
    authentication_timestamp = millis();
}

void RfidTask::waitForTag(){
    if(!config_rfid_enabled())
        return;
    waitingForTag = true;
    waitingBegin = millis();
    lcd.display("Waiting for RFID", 0, 0, RFID_ADD_WAITINGPERIOD, LCD_CLEAR_LINE);

    StaticJsonDocument<128> event;
    event["rfid_waiting"] = RFID_ADD_WAITINGPERIOD / 1000;
    event_send(event);
}

void RfidTask::updateEvseClaim() {

    if (!config_rfid_enabled()) {
        _evse->release(EvseClient_OpenEVSE_RFID);
        return;
    }
    
    if (isAuthenticated()) {
        _evse->release(EvseClient_OpenEVSE_RFID);
    } else {
        EvseState evseState = EvseState::Disabled;
        EvseProperties evseProperties {evseState};
        _evse->claim(EvseClient_OpenEVSE_RFID, EvseManager_Priority_RFID, evseProperties);
    }
}

void RfidTask::setOnCardScanned(std::function<bool(const String& idTag)> *onCardScanned) {
    this->onCardScanned = onCardScanned;
}

bool RfidTask::communicationFails() {
    return _rfid->readerFailure();
}

bool RfidReaderNullDevice::readerFailure() {
    return config_rfid_enabled();
}

RfidReaderNullDevice rfidNullDevice;

RfidTask rfid;
