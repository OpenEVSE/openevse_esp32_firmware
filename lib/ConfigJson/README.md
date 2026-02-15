# ConfigJson - ArduinoJson v7 Compatible

This is a local override of the ConfigJson library (v0.0.6) with modifications to support ArduinoJson v7.

## Changes from v0.0.6

1. Replaced all `DynamicJsonDocument` with `JsonDocument`
2. Removed all `JSON_OBJECT_SIZE()` and `JSON_ARRAY_SIZE()` capacity calculations (auto-managed in v7)
3. Replaced `containsKey()` with `is<T>()` type checking (more type-safe in v7)

## Why Local Override?

The upstream ConfigJson library (https://github.com/jeremypoulter/ConfigJson) currently only supports ArduinoJson v6. This local version provides v7 compatibility until the upstream library is updated.

## Migration Path

Once jeremypoulter/ConfigJson is updated to support ArduinoJson v7, this local override can be removed and platformio.ini can be updated to use the registry version again.

## Version

Based on ConfigJson v0.0.6 (commit f4f572a8a56bd3b7ce710c86613adc95e1dbf448)
Modified for ArduinoJson v7.4.2 compatibility
