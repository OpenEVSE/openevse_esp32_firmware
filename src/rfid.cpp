#define STATUS_NOT_ENABLED 0
#define STATUS_NOT_FOUND 1
#define STATUS_ACTIVE 2

#include "rfid.h"

#include "debug.h"
#include "mqtt.h"
#include "lcd.h"
#include "app_config.h"
#include "RapiSender.h"
#include "input.h"
#include "openevse.h"
#include "sleep_timer.h"
#include "Wire.h"

#ifndef I2C_SDA
#define I2C_SDA 21
#endif

#ifndef I2C_SCL
#define I2C_SCL 22
#endif

DFRobot_PN532_IIC  nfc(PN532_IRQ, PN532_POLLING); 
struct card NFCcard;
extern RapiSender rapiSender;

uint8_t status = STATUS_NOT_ENABLED;
boolean hasContact = false;

unsigned long nextScan = 0;

// How many more seconds to wait for tag
uint8_t waitingForTag = 0;
unsigned long stopWaiting = 0;
boolean cardFound = false;



void rfid_setup(){
    if(!config_rfid_enabled()){
        status = STATUS_NOT_ENABLED;
        return;
    }

    Wire.begin(I2C_SDA, I2C_SCL);
    if(nfc.begin()){
        status = STATUS_ACTIVE;
    }else{
        if(status == STATUS_NOT_FOUND){
            config_save_rfid(false, rfid_storage);
            DEBUG.println("RFID still not responding and has been disabled.");
        }else{
            DEBUG.println("RFID module did not respond!");
            status = STATUS_NOT_FOUND;
        }
    }
}

String getUidHex(card NFCcard){
    String uidHex = "";
    for(int i = 0; i < NFCcard.uidlenght; i++){
        char hex[NFCcard.uidlenght * 3];
        sprintf(hex,"%x",NFCcard.uid[i]);
        uidHex = uidHex + hex + " ";
    }
    uidHex.trim();
    return uidHex;
}

String getUidBytes(card NFCcard){
    String bytes = "";
    bytes.concat((char)NFCcard.uidlenght);
    for(int i = 0; i < NFCcard.uidlenght; i+= 1){
        bytes.concat((char)NFCcard.uid[i]);
    }

    Serial.println();
    Serial.println(bytes);
    Serial.println();

    const char* ptr = bytes.c_str();
    for(int i = 0; i < 4; i++){
        Serial.print((int)ptr[i], HEX);
    }

    return bytes;
}

void scanCard(){
    NFCcard = nfc.getInformation();
    String uidHex = getUidHex(NFCcard);

    if(waitingForTag > 0){
        waitingForTag = 0;
        cardFound = true;
        lcd_display("Tag detected!", 0, 0, 0, LCD_CLEAR_LINE);
        lcd_display(uidHex, 0, 1, 3000, LCD_CLEAR_LINE);
        sleep_timer_display_updates(true);
    }else{
        // Check if tag is stored locally
        char storedTags[rfid_storage.length() + 1];
        rfid_storage.toCharArray(storedTags, rfid_storage.length()+1);
        char* storedTag = strtok(storedTags, ",");
        while(storedTag)
        {
            String storedTagStr = storedTag;
            storedTagStr.trim();
            uidHex.trim();
            if(storedTagStr.compareTo(uidHex) == 0){
                    rapiSender.sendCmd(F("$FE"));
                break;
            }
            storedTag = strtok(NULL, ",");
        }

        // Send to MQTT broker
        DynamicJsonDocument data(4096);
        data["rfid"] = uidHex;
        mqtt_publish(data);
    }
}

void rfid_loop(){
    if(!config_rfid_enabled()){
        if(status != STATUS_NOT_FOUND)
            status = STATUS_NOT_ENABLED;
        return;
    }

    if(millis() < nextScan){
        return;
    }

    if(status != STATUS_ACTIVE){
        rfid_setup();
        return;
    }

    if(waitingForTag > 0){
        waitingForTag = (stopWaiting - millis()) / 1000;
        String msg = "tag... ";
        msg.concat(waitingForTag);
        lcd_display("Waiting for RFID", 0, 0, 0, LCD_CLEAR_LINE);
        lcd_display(msg, 0, 1, 1000, LCD_CLEAR_LINE);
        //if(waitingForTag < 1)
        //    sleep_timer_display_updates(true);
    }

    nextScan = millis() + SCAN_FREQ;

    boolean foundCard = (state >= OPENEVSE_STATE_SLEEPING || waitingForTag) && nfc.scan();

    if(foundCard && !hasContact){
        scanCard();
        hasContact = true;
        return;
    }

    if(!foundCard && hasContact){
        hasContact = false;
    }
}

uint8_t rfid_status(){
    return status;
}

void rfid_wait_for_tag(uint8_t seconds){
    if(!config_rfid_enabled())
        return;/*
    sleep_timer_display_updates(false);
    lcd_release();*/
    waitingForTag = seconds;
    stopWaiting = millis() + seconds * 1000;
    cardFound = false;
    lcd_display("Waiting for RFID", 0, 0, seconds * 1000, LCD_CLEAR_LINE);
}

DynamicJsonDocument rfid_poll() {
    const size_t capacity = JSON_ARRAY_SIZE(3);
    DynamicJsonDocument doc(capacity);
    if(waitingForTag > 0){
        // respond with remainding time
        doc["status"] = "waiting";
        doc["timeLeft"] = waitingForTag;
    } 
    else if(cardFound){
        // respond with the scanned tags uid and reset
        doc["status"] = "done";
        doc["scanned"] = getUidHex(NFCcard);
        cardFound = false;
    }
    else  {
        doc["status"] = "notStarted";
    }
    return doc;
}