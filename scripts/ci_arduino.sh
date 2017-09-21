#!/bin/sh -e
# Build with the default config with Arduino IDE

if [ -z $INO_ENV ]; then
  echo "INO_ENV not set"
  exit 1
fi
if [ -z $BUILD_TARGET ]; then
  echo "BUILD_TARGET not set"
  exit 1
fi

echo "@@@@@@" Testing $INO_ENV [$BUILD_TARGET]
arduino --verbose --verify --board $BUILD_TARGET $INO_ENV
