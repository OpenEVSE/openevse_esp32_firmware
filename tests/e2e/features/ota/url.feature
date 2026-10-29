@ota
Feature: Firmware fetched from a URL comes only from GitHub
  The device can fetch an update from a URL. It must only ever fetch from
  GitHub over HTTPS, and must install exactly the release GitHub published.

  Scenario Outline: The device refuses to fetch firmware from <case>
    When I ask the device to update from the refused URL "<case>"
    Then the device refuses the update
    And the device did not start writing an image

    Examples: URLs outside the allowed origin
      | case                                       |
      | plain HTTP to GitHub                       |
      | another host                               |
      | a host that starts with github.com         |
      | a host that puts github.com in the login   |
      | a non-HTTP scheme                          |

  @network
  Scenario: A GitHub release URL starts a download, not an upload
    When I ask the device to update from the published v5.1.5 release asset
    Then the device starts the update
    And the updater was started from GitHub

  @live @network
  Scenario: The published release downloads intact from GitHub
    Given the latest published firmware "openevse_wifi_v1.bin" from GitHub
    When I ask the device to update from that release asset
    Then the device starts the update
    And the updater was told to expect exactly the size GitHub published
    And the device received the image byte for byte
    And the image matches the SHA-256 GitHub publishes for the asset
