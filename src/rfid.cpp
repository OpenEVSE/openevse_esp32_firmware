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

/*
 * Documentation of the I2C messages:
 * NXP PN532 manual, https://www.nxp.com/docs/en/user-guide/141520.pdf
 * (accessed: 2022-01-14)
 */

#define PN532_I2C_ADDRESS  (0x48 >> 1)
#define PN532_RDY 0x01
#define PN532_FRAME_HEADER_LEN 4
#define PN532_FRAME_IDENTIFIER_LEN 1
#define PN532_CMD_SAMCONFIGURATION_RESPONSE 0x15
#define PN532_CMD_INAUTOPOLL_RESPONSE 0x61

#define I2C_READ_BUFFSIZE 35 //might increase later

RfidTask::RfidTask() :
  MicroTasks::Task()
{
}

void RfidTask::begin(EvseManager &evse, TwoWire& wire){
    _evse = &evse;
    i2c = &wire;
    MicroTask.startTask(this);
}

void RfidTask::setup(){
    if (config_rfid_enabled()) {
        pn532_initialize();
    }
}

void RfidTask::scanCard(String& uid){
    if(waitingForTag > 0){
        waitingForTag = 0;
        cardFound = true;
        waitingForTagResult = uid;
        lcd.display("Tag detected!", 0, 0, 0, LCD_CLEAR_LINE);
        lcd.display(uid, 0, 1, 3000, LCD_CLEAR_LINE);
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

    pn532_read();

    if (!config_rfid_enabled()) {
        pn532_status = PN532_DeviceStatus::NOT_ACTIVE;
        resetAuthentication();
        return SCAN_FREQ;
    }

    if (pn532_status == PN532_DeviceStatus::ACTIVE && millis() - pn532_lastResponse > MAXIMUM_UNRESPONSIVE_TIME) {
        DBUGLN(F("[rfid] connection with PN532 lost"));
        pn532_status = PN532_DeviceStatus::FAILED;
    }
    
    if (pn532_status != PN532_DeviceStatus::ACTIVE) {
        pn532_initialize();
        return SCAN_FREQ;
    }

    if(waitingForTag > 0){
        waitingForTag = (stopWaiting - millis()) / 1000;
        String msg = "tag... ";
        msg.concat(waitingForTag);
        lcd.display("Waiting for RFID", 0, 0, 0, LCD_CLEAR_LINE);
        lcd.display(msg, 0, 1, 1000, LCD_CLEAR_LINE);
    }

    if (pn532_pollCount >= AUTO_REFRESH_CONNECTION) {
        pn532_initialize();
        pn532_pollCount = 0;
    } else {
        pn532_poll();
        pn532_pollCount++;
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
    const size_t capacity = JSON_OBJECT_SIZE(2) + waitingForTagResult.length() + 1;
    DynamicJsonDocument doc(capacity);
    if(waitingForTag > 0){
        // respond with remainding time
        doc["status"] = "waiting";
        doc["timeLeft"] = waitingForTag;
    } 
    else if(cardFound){
        // respond with the scanned tags uid and reset
        doc["status"] = "done";
        doc["scanned"] = waitingForTagResult;
        cardFound = false;
        waitingForTagResult.clear();
    }
    else  {
        doc["status"] = "notStarted";
    }
    return doc;
}

void RfidTask::setOnCardScanned(std::function<bool(const String& idTag)> *onCardScanned) {
    this->onCardScanned = onCardScanned;
}

bool RfidTask::communicationFails() {
    return config_rfid_enabled() && pn532_status == PN532_DeviceStatus::FAILED;
}

void RfidTask::pn532_initialize() {

    /*
     * Command hardcoded according to NXP manual, page 89
     */
    uint8_t SAMConfiguration [] = {0x00, 0xFF, 0x05, 0xFB, 
                                   0xD4, 0x14, 0x01, 0x00, 0x00,
                                   0x17};
    i2c->beginTransmission(PN532_I2C_ADDRESS);
    i2c->write(SAMConfiguration, sizeof(SAMConfiguration) / sizeof(*SAMConfiguration));
    i2c->endTransmission();

    pn532_listen = true;
    //"flush" ACK
    delay(30);
    pn532_read();
}

void RfidTask::pn532_poll() {
    /*
     * Command hardcoded according to NXP manual, page 144
     */
    uint8_t InAutoPoll [] = {0x00, 0xFF, 0x06, 0xFA, 
                             0xD4, 0x60, 0x01, 0x01, 0x10, 0x20,
                             0x9A};
    i2c->beginTransmission(PN532_I2C_ADDRESS);
    i2c->write(InAutoPoll, sizeof(InAutoPoll) / sizeof(*InAutoPoll));
    i2c->endTransmission();

    pn532_listen = true;
    //"flush" ACK
    delay(30);
    pn532_read();
}

void RfidTask::pn532_read() {
    if (!pn532_listen)
        return;

    i2c->requestFrom(PN532_I2C_ADDRESS, I2C_READ_BUFFSIZE + 1, 1);
    if (i2c->read() != PN532_RDY) {
        //no msg available
        return;
    }

    uint8_t response [I2C_READ_BUFFSIZE] = {0};
    uint8_t frame_len = 0;
    while (i2c->available() && frame_len < I2C_READ_BUFFSIZE) {
        response[frame_len] = i2c->read();
        frame_len++;
    }
    i2c->flush();

    /*
     * For frame layout, see NXP manual, page 28
     */

    if (frame_len < PN532_FRAME_HEADER_LEN) {
        //frame corrupt
        pn532_listen = false;
        return;
    }

    uint8_t *header = nullptr;
    uint8_t header_offs = 0;
    while (header_offs < frame_len - PN532_FRAME_HEADER_LEN) {
        if (response[header_offs] == 0x00 && response[header_offs + 1] == 0xFF) { //find frame preamble
            header = response + header_offs; //header preamble found
            break;
        }
        header_offs++;
    }

    if (!header) {
        //invalid packet
        pn532_listen = false;
        return;
    }

    //check for ack frame
    if (header[2] == 0x00 && header[3] == 0xFF) {
        //ack detected
        return;
    }

    if (header[2] == 0x00 || header[2] == 0xFF) {
        //ignore frame type
        pn532_listen = false;
        return;
    }

    uint8_t data_len = header[2];
    uint8_t data_lcs = header[3];
    data_lcs += data_len;

    if (data_lcs) {
        //checksum wrong
        pn532_listen = false;
        return;
    }

    if (data_len <= PN532_FRAME_IDENTIFIER_LEN) {
        //ingore frame type
        pn532_listen = false;
        return;
    }

    //frame layout: <header><frame_identifier><data><data_checksum>
    uint8_t *frame_identifier = header + PN532_FRAME_HEADER_LEN;
    uint8_t *data = frame_identifier + PN532_FRAME_IDENTIFIER_LEN;
    uint8_t *data_dcs = frame_identifier + data_len;

    if (data_dcs - response >= frame_len) {
        DBUGLN(F("[rfid] Did not read sufficient bytes. Abort"));
        pn532_listen = false;
        return;
    }

    uint8_t data_checksum = *data_dcs;
    for (uint8_t *i = frame_identifier; i != data_dcs; i++) {
        data_checksum += *i;
    }

    if (data_checksum) {
        //wrong checksum
        pn532_listen = false;
        return;
    }

    if (frame_identifier[0] != 0xD5) {
        //ignore frame type
        pn532_listen = false;
        return;
    }

    uint8_t cmd_code = data[0];

    if (cmd_code == PN532_CMD_SAMCONFIGURATION_RESPONSE) {
        //success
        DBUGLN(F("[rfid] connection with PN532 active"));
        pn532_status = PN532_DeviceStatus::ACTIVE;
        pn532_lastResponse = millis();

    } else if (cmd_code == PN532_CMD_INAUTOPOLL_RESPONSE) {

        /*
         * see NXP manual, page 145
         */

        if (data_len < 3 || data[1] == 0) {
            pn532_hasContact = false;
            pn532_listen = false;
            return;
        }

        if (data_len < 10) {
            pn532_listen = false;
            return;
        }

        uint8_t card_type = data[2];
        uint8_t targetDataLen = data[3];
        uint8_t uidLen = data[8];

        if (data_len - 5 < targetDataLen || targetDataLen - 5 < uidLen) {
            DBUGLN(F("[rfid] INAUTOPOLL format err"));
            pn532_listen = false;
            return;
        }

        pn532_lastResponse = millis(); //successfully read

        if (pn532_hasContact) {
            //valid card already scanned the last time; nothing to do
            pn532_listen = false;
            return;
        }

        String uid = String('\0');
        uid.reserve(2*uidLen);

        for (uint8_t i = 0; i < uidLen; i++) {
            uint8_t uid_i = data[9 + i];
            uint8_t lhw = uid_i / 0x10;
            uint8_t rhw = uid_i % 0x10;
            uid += (char) (lhw <= 9 ? lhw + '0' : lhw%10 + 'a');
            uid += (char) (rhw <= 9 ? rhw + '0' : rhw%10 + 'a');
        }

        DBUG(F("[rfid] found card! type = "));
        DBUG(card_type);
        DBUG(F(", uid = "));
        DBUG(uid);
        DBUGLN(F(" end"));

        scanCard(uid);
        pn532_hasContact = true;

    } else {
        DBUG(F("[rfid] unknown response; cmd_code = "));
        DBUGLN(cmd_code);
    }

    pn532_listen = false;
    return;
}

RfidTask rfid;
