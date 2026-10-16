@ota
Feature: Firmware upload delivers the image intact
  Uploading a firmware file (the web UI, or curl -F to /update) must hand the
  exact image to the updater, so the charger installs what was uploaded.

  @smoke
  Scenario Outline: Uploading a firmware image of <size> bytes delivers it byte for byte
    Given a firmware image of <size> bytes
    When I upload the image to the firmware update endpoint
    Then the device accepts the upload
    And the device received the image byte for byte
    And the device reports the update as completed

    Examples: Sizes that exercise different flash write boundaries
      | size    |
      | 4096    |
      | 65536   |
      | 65537   |
      | 1835008 |

  @smoke
  Scenario Outline: Uploading a firmware image of <size> bytes tells the updater its exact size
    Given a firmware image of <size> bytes
    When I upload the image to the firmware update endpoint
    Then the device accepts the upload
    And the updater was told to expect exactly the image size

    Examples: Sizes that exercise different flash write boundaries
      | size    |
      | 4096    |
      | 65536   |
      | 65537   |
      | 1835008 |
