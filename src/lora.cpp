#if defined(ENABLE_LORA)

#include <Arduino.h>
#include <LoRa.h>
#include "emonesp.h"
#include "app_config.h"

#include "lora.h"


int loraAvailable = 0;
int lastAnnounce = 0;


void lora_setup()
{
	DBUG("LoRa Radio Init...");
	SPI.begin(SCK,MISO,MOSI,SS);
	LoRa.setPins(SS,RST_LoRa,DIO0);
	if (!LoRa.begin(LORA_BAND,LORA_PABOOST)) {
		DBUGLN("Error. LoRa Init failure!");
		return;
	}
	DBUGLN("OK. LoRa Radio Online!");
	loraAvailable = 1;
}


void lora_loop()
{
	if (!loraAvailable) {
		return;
	}

	LoRa.beginPacket();
	LoRa.setTxPower(14,RF_PACONFIG_PASELECT_PABOOST);
	LoRa.printf("%s Online!", esp_hostname.c_str());
	LoRa.endPacket();
}
	

#endif
