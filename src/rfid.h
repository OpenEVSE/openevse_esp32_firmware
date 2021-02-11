#include <DFRobot_PN532.h>
#include <ArduinoJson.h>

#define  SCAN_FREQ            200
#define  RFID_BLOCK_SIZE      16
#define  PN532_IRQ            (2)
#define  PN532_INTERRUPT      (1)
#define  PN532_POLLING        (0)

void rfid_setup();
void rfid_loop();
uint8_t rfid_status();

void rfid_store_tag(String uid);
DynamicJsonDocument rfid_get_stored_tags();
void rfid_wait_for_tag(uint8_t seconds);
DynamicJsonDocument rfid_poll();