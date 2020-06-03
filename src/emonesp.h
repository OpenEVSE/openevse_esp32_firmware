#ifndef _EMONESP_H
#define _EMONESP_H

// -------------------------------------------------------------------
// General support code used by all modules
// -------------------------------------------------------------------

#include "debug.h"
#include "profile.h"

#ifndef RAPI_PORT
#ifdef ESP32
#define RAPI_PORT Serial1
#elif defined(ESP8266)
#define RAPI_PORT Serial
#else
#error Platform not supported
#endif
#endif

#ifdef WIFI_LED
#ifndef WIFI_LED_ON_STATE
#define WIFI_LED_ON_STATE LOW
#endif

#ifndef WIFI_LED_AP_TIME
#define WIFI_LED_AP_TIME 1000
#endif

#ifndef WIFI_LED_AP_CONNECTED_TIME
#define WIFI_LED_AP_CONNECTED_TIME 100
#endif

#ifndef WIFI_LED_STA_CONNECTING_TIME
#define WIFI_LED_STA_CONNECTING_TIME 500
#endif
#endif

#ifndef WIFI_BUTTON
#define WIFI_BUTTON 0
#endif

#ifndef WIFI_BUTTON_PRESSED_STATE
#define WIFI_BUTTON_PRESSED_STATE LOW
#endif

#ifndef WIFI_BUTTON_PRESSED_PIN_MODE
#if LOW == WIFI_BUTTON_PRESSED_STATE
#define WIFI_BUTTON_PRESSED_PIN_MODE INPUT_PULLUP
#else
#define WIFI_BUTTON_PRESSED_PIN_MODE INPUT_PULLDOWN
#endif
#endif

#ifndef WIFI_BUTTON_AP_TIMEOUT
#define WIFI_BUTTON_AP_TIMEOUT              (2 * 1000)
#endif

#ifndef WIFI_BUTTON_FACTORY_RESET_TIMEOUT
#define WIFI_BUTTON_FACTORY_RESET_TIMEOUT   (10 * 1000)
#endif

#ifndef WIFI_CLIENT_RETRY_TIMEOUT
#define WIFI_CLIENT_RETRY_TIMEOUT (5 * 60 * 1000)
#endif

// Used to change the ADC channel used for seeding the rndom number generator
// Should be set to an unconnected pin
#ifndef RANDOM_SEED_CHANNEL
#if WIFI_BUTTON != 0 && (!defined(WIFI_LED) || WIFI_LED != 0)
#define RANDOM_SEED_CHANNEL 0
#else
#define RANDOM_SEED_CHANNEL 1
#endif
#endif

#ifdef ONBOARD_LEDS
#ifndef ONBOARD_LED_ON_STATE
#define ONBOARD_LED_ON_STATE  WIFI_LED_ON_STATE
#endif
#endif

#ifndef HAL_ID_ENCODING_BASE
#define HAL_ID_ENCODING_BASE 16
#endif

#ifndef HAL_SHORT_ID_LENGTH
#define HAL_SHORT_ID_LENGTH 4
#endif

#ifndef SNTP_DEFAULT_HOST
#define SNTP_DEFAULT_HOST "pool.ntp.org"
#endif

#ifndef DEFAULT_TIME_ZONE
// Default time zone, Europe/London
#define DEFAULT_TIME_ZONE "Europe/London|GMT0BST,M3.5.0/1,M10.5.0"
#endif

#ifndef DEFAULT_VOLTAGE
#define DEFAULT_VOLTAGE  240
#endif

#ifdef NO_SENSOR_SCALING

#ifndef VOLTS_SCALE_FACTOR
#define VOLTS_SCALE_FACTOR  1.0
#endif

#ifndef AMPS_SCALE_FACTOR
#define AMPS_SCALE_FACTOR   1.0
#endif

#ifndef TEMP_SCALE_FACTOR
#define TEMP_SCALE_FACTOR   1.0
#endif

#else

#ifndef VOLTS_SCALE_FACTOR
#define VOLTS_SCALE_FACTOR  1.0
#endif

#ifndef AMPS_SCALE_FACTOR
#define AMPS_SCALE_FACTOR   1000.0
#endif

#ifndef TEMP_SCALE_FACTOR
#define TEMP_SCALE_FACTOR   10.0
#endif

#endif

#endif // _EMONESP_H
