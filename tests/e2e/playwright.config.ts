import { defineConfig } from '@playwright/test';
import { defineBddConfig } from 'playwright-bdd';

const testDir = defineBddConfig({
  features: 'features/**/*.feature',
  steps: ['steps/**/*.ts', 'support/fixtures.ts'],
});

// Each worker owns a firmware + emulator pair (see support/stack.ts), so
// scenarios run in parallel without sharing a charger.
export default defineConfig({
  testDir,
  timeout: 60_000,
  expect: { timeout: 10_000 },
  workers: process.env.CI ? 2 : undefined,
  retries: 0, // a retry would hide a flaky charger interaction rather than fix it
  forbidOnly: !!process.env.CI,
  outputDir: 'test-results',
  reporter: [
    ['list'],
    ['html', { open: 'never' }],
    ['junit', { outputFile: 'output/junit.xml' }],
  ],
  use: {
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
    video: 'retain-on-failure',
    // Set CHROMIUM_PATH to use a system Chromium instead of Playwright's own download.
    launchOptions: process.env.CHROMIUM_PATH ? { executablePath: process.env.CHROMIUM_PATH } : {},
  },
});
