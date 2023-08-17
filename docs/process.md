# OpenEVSE Process Documentation

## Change management

Change requests and bug reports are are submitted through the [GitHub issue tracker](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/issues). There should be a single issue/bug/feature per ticket.

All changes should be made on a branch or fork of the `master` branch and the change should be submitted through a GitHub pull request. Pull requests will be reviewed by a repository administrator before being merged into the `master` branch. For changes by a repository administrator, the pull request should be reviewed by another administrator before being merging.

## Building

Instructions on building the firmware an be found in the [Developer Guide](developer-guide.md).

For releases and PR approval the builds are built using the [Build/Release OpenEVSE](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/actions/workflows/build.yaml) workflow to ensure consistent behaviour.

## Testing

> TODO

## Creating a new Releases

1. Ensure GitHub actions are complete and green
    [![Build/Release OpenEVSE](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/actions/workflows/build.yaml/badge.svg)](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/actions/workflows/build.yaml)
1. Create a new [GitHub release](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/releases/new) entering the tag name as the new [version number](https://semver.org/), for example: `v1.0.0`
1. Add the release notes, a good start is to use the `Auto-Generated Release Notes` and edit as needed
1. Select the `Pre-release` checkbox
1. Click `Publish release`
1. The GitHub build workflow will generate the binaries and upload them to the release
1. Remove any unwanted binaries, need at least:
    - `olimex_esp32-gateway-e.bin`
    - `adafruit_huzzah32.bin`
    - `openevse_wifi_v1.bin`
    - `espressif_esp-wrover-kit`
    - `nodemcu-32s`
1. Test the uploaded binaries
1. Unselect the `Pre-release` checkbox
1. Click `Update release`
1. Ensure the release validation action is green
    [![Release Validation](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/actions/workflows/release_validation.yaml/badge.svg)](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/actions/workflows/release_validation.yaml)
