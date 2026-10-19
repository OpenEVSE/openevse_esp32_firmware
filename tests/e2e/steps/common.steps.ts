import { Given, When, Then, expect } from '../support/fixtures';
import { waitFor } from '../support/clients';

// ---- Charger / device setup -------------------------------------------------

Given('a brand new charger', async () => {
  // Every scenario starts from factory-fresh firmware (see support/fixtures.ts).
});

Given('the charger has completed first-run setup', async ({ world }) => {
  await world.device.setConfig({ wizard_passed: true });
});

Given('the simulation runs {int} times faster than real time', async ({ world }, scale: number) => {
  await world.emulator.setTimeScale(scale);
});

// ---- The vehicle (keywords are interchangeable: Given/When share one definition) ----

Given('a vehicle is plugged in', async ({ world }) => {
  await world.emulator.connectVehicle();
});

Given('a vehicle is plugged in and asks for charge', async ({ world }) => {
  await world.emulator.connectVehicle();
  await world.emulator.requestCharge();
});

Given("the vehicle's battery is at {int}% and it will only charge to {int}%", async ({ world }, soc: number, limit: number) => {
  await world.emulator.setSoc(soc);
  await world.emulator.setChargeLimit(limit);
});

When('the vehicle is unplugged', async ({ world }) => {
  await world.emulator.disconnectVehicle();
});

When('the charger detects a GFCI trip', async ({ world }) => {
  await world.emulator.triggerFault('gfci');
});

// ---- Backend assertions (the firmware's own API, independent of the UI) ------

// EVSE state codes reported by the firmware's /status endpoint.
const STATES: Record<string, number> = {
  'not connected': 1,
  connected: 2,
  charging: 3,
  'ventilation required': 4,
  'GFCI fault': 6,
  sleeping: 254,
  disabled: 255,
};

Then('the charger reports it is {string}', async ({ world }, state: string) => {
  const expected = STATES[state];
  if (expected === undefined) throw new Error(`Unknown charger state "${state}"`);
  await waitFor(
    `charger state "${state}"`,
    async () => (await world.device.status()).state === expected,
  ).catch(async (e) => {
    throw new Error(`${e.message}; charger state is ${(await world.device.status()).state}`);
  });
});

Then('the charger is offering {int} A to the vehicle', async ({ world }, amps: number) => {
  await waitFor(
    `${amps} A to be offered`,
    async () => (await world.emulator.status()).evse.current_capacity === amps,
  );
});

Then('the vehicle is not receiving a charge', async ({ world }) => {
  await waitFor(
    'the vehicle to stop drawing current',
    async () => (await world.emulator.status()).evse.actual_current === 0,
  );
});

Then("the charger's override is {int} A", async ({ world }, amps: number) => {
  await waitFor(
    `override of ${amps} A`,
    async () => (await world.device.override()).charge_current === amps,
  );
});

Then('the charger has an energy limit of {int} Wh', async ({ world }, wh: number) => {
  await waitFor('an energy limit', async () => {
    const limit = await world.device.limit();
    return limit.type === 'energy' && limit.value === wh;
  });
});

Then('the charger has delivered more than {int} Wh this session', async ({ world }, wh: number) => {
  await waitFor(
    `more than ${wh} Wh delivered`,
    async () => (await world.device.status()).watthour > wh,
    30_000,
  );
});

Then('the charger has recorded {int} GFCI trip', async ({ world }, count: number) => {
  await waitFor('a GFCI trip count', async () => (await world.device.status()).gfcicount === count);
});

Then("the charger's maximum current is {int} A", async ({ world }, amps: number) => {
  await waitFor('max current', async () => (await world.device.config()).max_current_soft === amps);
});
