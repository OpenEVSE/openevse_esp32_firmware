@route:/
Feature: Safety faults
  When the charger detects a fault it stops charging, tells the user, and
  refuses to be controlled until the fault is dealt with.

  Background:
    Given the charger has completed first-run setup
    And a vehicle is plugged in and asks for charge
    And I have the dashboard open

  @smoke
  Scenario: A GFCI trip stops charging and is reported
    When the charger detects a GFCI trip
    Then the dashboard shows "GFCI fault"
    And the dashboard shows "A GFCI trip was recorded"
    And the charger has recorded 1 GFCI trip
    And the vehicle is not receiving a charge

  Scenario: Controls are locked out while a fault is active
    When the charger detects a GFCI trip
    Then the dashboard shows "GFCI fault"
    And the charge mode controls are disabled
    And the charge rate control is disabled
