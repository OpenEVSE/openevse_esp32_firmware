@route:/
Feature: Charging state on the dashboard
  The home page shows what the charger and the car are doing, and the
  charger really does what the dashboard says.

  Background:
    Given the charger has completed first-run setup

  @smoke
  Scenario: An idle charger is ready for a car
    When I open the dashboard
    Then the dashboard shows "Not connected"
    And the dashboard shows "Plug in your car to start"
    And the charger reports it is "not connected"

  @smoke
  Scenario: Plugging in a car is noticed
    Given I have the dashboard open
    When a vehicle is plugged in
    Then the dashboard shows "Car connected"
    And the charger reports it is "connected"

  @smoke
  Scenario: A car that asks for charge is charged
    Given I have the dashboard open
    When a vehicle is plugged in and asks for charge
    Then the charger reports it is "charging"
    And the charger is offering 32 A to the vehicle
    And the dashboard shows a current of more than 0 A

  Scenario: Unplugging the car ends the session
    Given a vehicle is plugged in and asks for charge
    And I have the dashboard open
    When the vehicle is unplugged
    Then the charger reports it is "not connected"
    And the dashboard shows "Not connected"

  Scenario: Charging a vehicle adds to the session energy
    Given a vehicle is plugged in and asks for charge
    When I open the dashboard
    Then the charger has delivered more than 5 Wh this session
    And the dashboard shows more than 0 session kWh

  Scenario: A vehicle that is full stops asking for charge
    Given the simulation runs 600 times faster than real time
    And the vehicle's battery is at 79% and it will only charge to 80%
    And a vehicle is plugged in and asks for charge
    When I open the dashboard
    Then the charger reports it is "connected"
    And the dashboard shows "Car connected"
