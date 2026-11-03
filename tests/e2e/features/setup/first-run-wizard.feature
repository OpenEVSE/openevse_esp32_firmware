@route:/
Feature: First-run setup wizard
  A brand new charger walks its owner through setup before showing the
  dashboard.

  Background:
    Given a brand new charger

  @smoke
  Scenario: A new charger starts with the setup wizard
    When I open the dashboard
    Then the setup wizard shows step 1 of 6 "Welcome"

  Scenario: The wizard saves charger basics as it goes
    Given I have the dashboard open
    When I continue to the "Charger basics" step
    And I set the maximum current to 24 A
    And I continue to the "Time" step
    Then the charger's maximum current is 24 A
    And the setup wizard shows step 3 of 6 "Time"
