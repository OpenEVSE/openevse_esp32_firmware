/*
 * Author: Ammar Panjwani
 * Adapted from PN532 to work with Access Cards 
 * Weigand Format: https://www.pagemac.com/projects/rfid/hid_data_formats
 * 
 * If you have questions you can reach me at ammar.panj@gmail.com or 832-654-1839
 */

#if defined(ENABLE_AccessCard)

#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_NFCREADER)
#undef ENABLE_DEBUG
#endif

#include "AccessCard.h"
#include <Wire.h>
#include "app_config.h"
#include "debug.h"
#include "lcd.h"

#ifndef SCAN_DELAY
#define SCAN_DELAY              1000
#endif
#ifndef ACK_DELAY
#define ACK_DELAY               30
#endif
#ifndef MAXIMUM_UNRESPONSIVE_TIME
#define MAXIMUM_UNRESPONSIVE_TIME 60000UL
#endif
#ifndef AUTO_REFRESH_CONNECTION
#define AUTO_REFRESH_CONNECTION 30

#define UID_BRIDGE_I2C_ADDR   0x42    
#define UID_MAX_ASCII         32      
#endif

#define AccessCard_I2C_ADDRESS  (0x48 >> 1)
#define AccessCard_RDY 0x01
#define AccessCard_FRAME_HEADER_LEN 4
#define AccessCard_FRAME_IDENTIFIER_LEN 1
#define AccessCard_CMD_SAMCONFIGURATION_RESPONSE 0x15
#define AccessCard_CMD_INAUTOPOLL_RESPONSE 0x61

#define I2C_READ_BUFFSIZE 35 

AccessCard::AccessCard() : MicroTasks::Task() {
    
}

/**
 * @brief Connects all wires and intiates communication with daughter board
 */
void AccessCard::begin() {  
    Wire.begin(I2C_SDA, I2C_SCL);  
    status = DeviceStatus::NOT_ACTIVE;
    lastResponse = millis();
    pollCount = 0;
    hasContact = false;
    MicroTask.startTask(this);
}

/**
 * @brief Runs while expecting an input from the daughter board. 
 * @param reason reminante of older version, left in to prevent errors
 */
unsigned long AccessCard::loop(MicroTasks::WakeReason reason) { 

    if (!config_rfid_enabled()) {
        status = DeviceStatus::NOT_ACTIVE;
        return SCAN_DELAY;
    }

    if (status == DeviceStatus::ACTIVE && millis() - lastResponse > MAXIMUM_UNRESPONSIVE_TIME) {
        DBUGLN(F("[rfid] I2C bridge not responding"));
        lcd.display("RFID source not found", 0, 1, 5 * 1000, LCD_CLEAR_LINE);
        status = DeviceStatus::FAILED;
    }

    if (pollCount >= AUTO_REFRESH_CONNECTION) {
        pollCount = 0;
    } else {
        pollCount++;
    }

    readFromBridge();

    return ACK_DELAY;
}

/**
 * @brief Reads from the I2C connection and sets the UID value
 */
void AccessCard::readFromBridge() {  
    const uint8_t want = 1 + UID_MAX_ASCII;

    uint8_t got = Wire.requestFrom((int)UID_BRIDGE_I2C_ADDR, (int)want, (int)true);
    if (got == 0) {
        return;
    }

    if (!Wire.available()) return;

    int len = Wire.read();              // first byte = length
    if (len <= 0) {
        // length 0 means "no new UID". Allow next new read to trigger.
        hasContact = false;
        status = DeviceStatus::ACTIVE; 
        lastResponse = millis();
        while (Wire.available()) (void)Wire.read();
        return;
    }

    if (len > UID_MAX_ASCII) len = UID_MAX_ASCII;

    String uid;
    uid.reserve(len);
    for (int i = 0; i < len && Wire.available(); i++) {
        uid += (char)Wire.read();
    }

    while (Wire.available()) (void)Wire.read();

    uid.toUpperCase();

    status = DeviceStatus::ACTIVE;
    lastResponse = millis();

    if (hasContact) return;

    onCardDetected(uid);
    hasContact = true;
}

/**
 * @brief Triggers in the event the reader cannot be reached.
 */
bool AccessCard::readerFailure() {
    return config_rfid_enabled() && status == DeviceStatus::FAILED;
}

/**
 * @brief Sets parameters for where and how to write data on start.
 */
void AccessCard::initialize() {
    uint8_t SAMConfiguration [] = {0x00, 0xFF, 0x05, 0xFB, 
                                   0xD4, 0x14, 0x01, 0x00, 0x00,
                                   0x17};
    Wire.beginTransmission(AccessCard_I2C_ADDRESS);
    Wire.write(SAMConfiguration, sizeof(SAMConfiguration) / sizeof(*SAMConfiguration));
    Wire.endTransmission();

    listen = true;
}

/**
 * @brief Tells the reader to start looking for Cards and how to deliver the data.
 */
void AccessCard::poll() {

    uint8_t InAutoPoll [] = {0x00, 0xFF, 0x06, 0xFA, 
                             0xD4, 0x60, 0x01, 0x01, 0x10, 0x20,
                             0x9A};
    Wire.beginTransmission(AccessCard_I2C_ADDRESS);
    Wire.write(InAutoPoll, sizeof(InAutoPoll) / sizeof(*InAutoPoll));
    Wire.endTransmission();

    listen = true;
}


AccessCard accessCard;

#endif
