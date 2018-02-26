#!/bin/sh -e
# Build the config specified by PIO_ENV

if [ -z $PIO_ENV ]; then
  echo "PIO_ENV not set"
  exit 1
fi

echo travis_fold:start:firmware
platformio run -e $PIO_ENV
echo travis_fold:end:firmware

# echo travis_fold:start:spiffs
# platformio run -e $PIO_ENV -t buildfs
# echo travis_fold:end:spiffs
