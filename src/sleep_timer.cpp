#include "sleep_timer.h"
#include "input.h"
#include "lcd.h"
#include "app_config.h"
#include <openevse.h>

unsigned long nextTimerTick = 0;
unsigned long goToSleep = 0;
bool counting = true;
bool displayUpdates = true;

String createTimeString(uint16_t seconds){
    String str = "";
    uint8_t min = seconds / 60;
    uint8_t sec = seconds % 60;
    str.concat(min / 10);
    str.concat(min % 10);
    str.concat(":");
    str.concat(sec / 10);
    str.concat(sec % 10);
    return str;
}

void updateDisplay(){
    uint8_t messageToDisplay = millis() / 2000 % 4;
    if(messageToDisplay == 0 && state == OPENEVSE_STATE_NOT_CONNECTED){
        lcd_display("Connect your", 0, 0, 0, LCD_CLEAR_LINE);
        lcd_display("vehicle", 0, 1, 1100, LCD_CLEAR_LINE);
    }else if(messageToDisplay == 0 && state == OPENEVSE_STATE_CONNECTED){
        lcd_display("Not Charging", 0, 1, 1100, LCD_CLEAR_LINE);
    }else if(messageToDisplay == 1 && (state == OPENEVSE_STATE_NOT_CONNECTED || state == OPENEVSE_STATE_CONNECTED)){
        lcd_display("Going to", 0, 0, 0, LCD_CLEAR_LINE);
        String timerMsg = "sleep in: ";
        int timeLeft = (goToSleep - millis()) / 1000;
        timerMsg.concat(createTimeString(timeLeft));
        lcd_display(timerMsg, 0, 1, 1100, LCD_CLEAR_LINE);
    }else if(messageToDisplay == 0 && state >= OPENEVSE_STATE_SLEEPING){
        lcd_display("Scan RFID tag", 0, 0, 0, LCD_CLEAR_LINE);
        lcd_display("to start", 0, 1, 1100, LCD_CLEAR_LINE);
    }
}

void sleep_timer_loop(){
    if (millis() < nextTimerTick)
        return;

    nextTimerTick = millis() + 1000;

    if(counting){
        if(millis() > goToSleep){
            // Simulate a button press in case there is a timer active
            rapiSender.sendCmd(F("$F1"));
            rapiSender.sendCmd(config_pause_uses_disabled() ? F("$FD") : F("$FS"));
            counting = false;
        }
    }

    if(displayUpdates){
        updateDisplay();
    }
}

void on_wake_up(){
    if((sleep_timer_enabled_flags & SLEEP_TIMER_NOT_CONNECTED_FLAG) == SLEEP_TIMER_NOT_CONNECTED_FLAG){
        goToSleep = millis() + sleep_timer_not_connected * 1000;
        counting = true;
    }
}

void on_vehicle_connected(){
    if((sleep_timer_enabled_flags & SLEEP_TIMER_CONNECTED_FLAG) == SLEEP_TIMER_CONNECTED_FLAG){
        goToSleep = millis() + sleep_timer_connected * 1000;
        counting = true;
    }
}

void on_vehicle_disconnected(){
    if((sleep_timer_enabled_flags & SLEEP_TIMER_DISCONNECTED_FLAG) == SLEEP_TIMER_DISCONNECTED_FLAG){
        goToSleep = millis() + sleep_timer_disconnected * 1000;
        counting = true;
    }
}

void sleep_timer_display_updates(bool enabled){
    displayUpdates = enabled;
}