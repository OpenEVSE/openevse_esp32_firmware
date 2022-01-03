/*
 * Author: Oliver Norin
 *         Matthias Akstaller
 */

#include "rfid.h"

#include "debug.h"
#include "mqtt.h"
#include "lcd.h"
#include "app_config.h"
#include "input.h"
#include "openevse.h"

RfidTask::RfidTask() :
  MicroTasks::Task(),
  nfc(PN532_IRQ, PN532_POLLING)
{
}

void RfidTask::begin(EvseManager &evse){
    _evse = &evse;
    MicroTask.startTask(this);
}

void RfidTask::setup(){
    if (config_rfid_enabled()) {
        wakeup();
    }
}

boolean RfidTask::wakeup(){
    bool awake = nfc.begin();
    if(awake){
        status = NfcDeviceStatus::ACTIVE;
    } else {
        status = NfcDeviceStatus::NOT_ACTIVE;
        DEBUG.println("RFID module did not respond!");
    }
    return awake;
}

void RfidTask::scanCard(){
    NFCcard = nfc.getInformation();
    String uid = nfc.readUid();
    uid.trim();

    if(waitingForTag > 0){
        waitingForTag = 0;
        cardFound = true;
        lcd.display("Tag detected!", 0, 0, 0, LCD_CLEAR_LINE);
        lcd.display(uid, 0, 1, 3000, LCD_CLEAR_LINE);
        DBUG(F("[rfid] Tag detected! "));
        DBUGLN(uid);
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
        const size_t UID_MAXLEN = 8; //hardcoded in DFRobot_PN532.h
        DynamicJsonDocument data{JSON_OBJECT_SIZE(1) + UID_MAXLEN + 1};
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
        return SCAN_FREQ;
    } else if (status != NfcDeviceStatus::ACTIVE) {
        wakeup();
        return SCAN_FREQ;
    }

    if(waitingForTag > 0){
        waitingForTag = (stopWaiting - millis()) / 1000;
        String msg = "tag... ";
        msg.concat(waitingForTag);
        lcd.display("Waiting for RFID", 0, 0, 0, LCD_CLEAR_LINE);
        lcd.display(msg, 0, 1, 1000, LCD_CLEAR_LINE);
    }
    
    //boolean foundCard = (evse.getEvseState() >= OPENEVSE_STATE_SLEEPING || waitingForTag) && nfc.scan();
    boolean foundCard = nfc.scan();

    if(foundCard && !hasContact){
        scanCard();
        hasContact = true;
        return SCAN_FREQ;
    }

    if(!foundCard && hasContact){
        hasContact = false;
    }

    return SCAN_FREQ;
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

void RfidTask::waitForTag(uint8_t seconds){
    if(!config_rfid_enabled())
        return;
    waitingForTag = seconds;
    stopWaiting = millis() + seconds * 1000;
    cardFound = false;
    lcd.display("Waiting for RFID", 0, 0, seconds * 1000, LCD_CLEAR_LINE);
}

void RfidTask::updateEvseClaim() {

    if (!config_rfid_enabled()) {
        _evse->release(EvseClient_OpenEVSE_RFID);
        return;
    }
    
    EvseState evseState = isAuthenticated() ? (EvseState::Active) : (EvseState::Disabled);

    EvseProperties evseProperties {evseState};

    _evse->claim(EvseClient_OpenEVSE_RFID, EvseManager_Priority_RFID, evseProperties);
}

DynamicJsonDocument RfidTask::rfidPoll() {
    const size_t UID_MAXLEN = 8; //hardcoded in DFRobot_PN532.h
    const size_t capacity = JSON_OBJECT_SIZE(2) + UID_MAXLEN + 1;
    DynamicJsonDocument doc(capacity);
    if(waitingForTag > 0){
        // respond with remainding time
        doc["status"] = "waiting";
        doc["timeLeft"] = waitingForTag;
    } 
    else if(cardFound){
        // respond with the scanned tags uid and reset
        doc["status"] = "done";
        doc["scanned"] = nfc.readUid();
        cardFound = false;
    }
    else  {
        doc["status"] = "notStarted";
    }
    return doc;
}

RfidTask rfid;
