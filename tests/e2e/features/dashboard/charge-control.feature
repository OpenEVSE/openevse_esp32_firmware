@route:/
Feature: Controlling charging from the dashboard
  The charge mode and charge rate controls change what the charger does,
  not just what the page displays.

  Background:
    Given the charger has completed first-run setup
    And a vehicle is plugged in and asks for charge
    And I have the dashboard open

  @smoke
  Scenario: Switching the charge mode to Off stops charging
    When I switch the charge mode to "Off"
    Then the charge mode "Off" is selected
    And the charger reports it is "sleeping"
    And the vehicle is not receiving a charge

  Scenario: Switching the charge mode back to Auto resumes charging
    Given I switch the charge mode to "Off"
    And the charger reports it is "sleeping"
    When I switch the charge mode to "Auto"
    Then the charge mode "Auto" is selected
    And the charger reports it is "charging"

  Scenario: Switching the charge mode to On keeps charging
    When I switch the charge mode to "On"
    Then the charge mode "On" is selected
    And the charger reports it is "charging"

  @smoke
  Scenario: Lowering the charge rate lowers the offer to the vehicle
    When I set the charge rate to 16 A
    Then the dashboard shows a charge rate of 16 A
    And the charger is offering 16 A to the vehicle
    And the charger's override is 16 A

  Scenario: Setting an energy limit is stored by the charger
    When I set an energy limit of 10 kWh
    Then the charger has an energy limit of 10000 Wh
