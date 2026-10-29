import { test as base, createBdd } from 'playwright-bdd';
import { Stack } from './stack';
import { Device, Emulator } from './clients';

/** Per-scenario state for firmware upgrade scenarios. */
export type OtaState = {
  /** The image the scenario delivers, byte for byte. */
  image?: Buffer;
  /** A published release asset, as GitHub describes it. */
  release?: { url: string; name: string; size: number; sha256: string };
  /** The HTTP response from the device's update endpoint. */
  response?: { status: number; body: string };
};

type World = {
  /** The charger's own HTTP API: assert backend state independently of the UI. */
  device: Device;
  /** The emulated charger hardware and car. */
  emulator: Emulator;
  stack: Stack;
  ota: OtaState;
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
    await use({ stack, device: stack.device, emulator: stack.emulator, ota: {} });
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
