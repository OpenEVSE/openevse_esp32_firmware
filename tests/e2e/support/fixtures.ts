import { test as base, createBdd } from 'playwright-bdd';
import { Stack } from './stack';
import { Device, Emulator } from './clients';

type World = {
  /** The charger's own HTTP API: assert backend state independently of the UI. */
  device: Device;
  /** The emulated charger hardware and car. */
  emulator: Emulator;
  stack: Stack;
};

export const test = base.extend<{ world: World; scenario: void }, { stack: Stack }>({
  // One firmware + emulator pair per worker, started once.
  stack: [
    async ({}, use, workerInfo) => {
      const stack = await Stack.start(workerInfo.workerIndex);
      await use(stack);
      await stack.stop();
    },
    { scope: 'worker', timeout: 120_000 },
  ],

  baseURL: async ({ stack }, use) => use(stack.deviceUrl),

  world: async ({ stack }, use) => {
    await use({ stack, device: stack.device, emulator: stack.emulator });
  },

  // Every scenario starts from factory-fresh firmware config and a disconnected car.
  scenario: [
    async ({ stack }, use) => {
      await stack.reset();
      await use();
    },
    { auto: true, timeout: 90_000 },
  ],
});

export const { Given, When, Then } = createBdd(test);
export { expect } from '@playwright/test';
