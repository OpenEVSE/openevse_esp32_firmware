#!/bin/bash

# Script to compile all released enviroments and copy build artifacts to project root for easy upload to github releases. 

pio run -e openevse_wifi_v1
cp .pio/build/openevse_wifi_v1/firmware.bin firmware.bin

pio run -e openevse_huzzah32
cp .pio/build/openevse_wifi_v1/firmware.bin openevse_huzzah32.bin

pio run -e openevse_esp32-gateway-e
cp .pio/build/openevse_wifi_v1/firmware.bin esp32-gateway-e.bin

echo "Done" 



