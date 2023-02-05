# OpenEVSE Process Documentation

## Change management

Change requests and bug reports are are submitted through the [GitHub issue tracker](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/issues). There should be a single issue/bug/feature per ticket.

All changes should be mage on a branch or fork of the `master` branch and the change should be submitted through a GitHub pull request. Pull requests will be reviewed by a repository administrator before being merged into the `master` branch. For changes by the repository administrator, the pull request should be reviewed by another administrator before being merging.

## Building

Instructions on building the firmware an be found in the [Developer Guide](developer-guide.md).

For releases and PR approval the builds are built using the [Build/Release OpenEVSE](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/actions/workflows/build.yaml) workflow to ensure consistent behaviour.

## Testing

> TODO

## Creating a new Releases

1. Ensure GitHub actions are complete and green
    [![Build/Release OpenEVSE](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/actions/workflows/build.yaml/badge.svg)](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/actions/workflows/build.yaml)
1. Check the [version number](https://semver.org/) is correct
1. Go to the latest [Development Build](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/releases/tag/latest) release
1. Edit the release
1. Change the tag from latest to the [version number](https://semver.org/), for example: `v1.0.0`
1. Change the branch to `latest`, is not automatically filled in so have to type the name in. Will show the appropriate commit hash on pressing enter
2. Add the release notes, a good start is to use the `Auto-Generated Release Notes` and edit as needed
3. Remove any unwanted binaries, need at least:
    - `openevse_esp32-gateway-e.bin`
    - `openevse_huzzah32.bin`
    - `openevse_wifi_v1.bin`
    - `openevse_esp-wrover-kit`
    - `openevse_nodemcu-32s`
4. Unselect the `Pre-release` checkbox
5. Click `Update release`
6. Ensure the release validation action is green
    [![Release Validation](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/actions/workflows/release_validation.yaml/badge.svg)](https://github.com/OpenEVSE/ESP32_WiFi_V4.x/actions/workflows/release_validation.yaml)
