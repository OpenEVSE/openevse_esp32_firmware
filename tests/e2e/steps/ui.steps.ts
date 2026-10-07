import { Given, When, Then, expect } from '../support/fixtures';

// ---- Navigation --------------------------------------------------------------

async function openDashboard(page: import('@playwright/test').Page) {
  await page.goto('/');
  // The app renders <main> once it has booted and loaded the charger's state.
  await expect(page.getByRole('main')).toBeVisible();
}

Given('I have the dashboard open', async ({ page }) => openDashboard(page));
Given('I open the dashboard', async ({ page }) => openDashboard(page));

// ---- Reading the dashboard ---------------------------------------------------

Then('the dashboard shows {string}', async ({ page }, text: string) => {
  await expect(page.getByText(text, { exact: false }).first()).toBeVisible();
});

Then('the dashboard shows a current of more than {int} A', async ({ page }, amps: number) => {
  await expect
    .poll(async () => {
      const text = await page.locator('main').innerText();
      const match = text.match(/([\d.]+)\s*A\s*\n?\s*CURRENT/i);
      return match ? Number(match[1]) : 0;
    })
    .toBeGreaterThan(amps);
});

Then('the dashboard shows more than {float} session kWh', async ({ page }, kwh: number) => {
  await expect
    .poll(async () => {
      const text = await page.locator('main').innerText();
      const match = text.match(/([\d.]+)\s*\n?\s*SESSION KWH/i);
      return match ? Number(match[1]) : 0;
    })
    .toBeGreaterThan(kwh);
});

Then('the dashboard shows a charge rate of {int} A', async ({ page }, amps: number) => {
  await expect(page.getByRole('button', { name: 'Charge rate' })).toContainText(`${amps} A`);
});

Then('the charge mode {string} is selected', async ({ page }, mode: string) => {
  await expect(page.getByRole('radio', { name: mode, exact: true })).toBeChecked();
});

Then('the charge mode controls are disabled', async ({ page }) => {
  for (const mode of ['Off', 'Auto', 'On']) {
    await expect(page.getByRole('radio', { name: mode, exact: true })).toBeDisabled();
  }
});

Then('the charge rate control is disabled', async ({ page }) => {
  await expect(page.getByRole('button', { name: 'Charge rate' })).toBeDisabled();
});

// ---- Operating the dashboard -------------------------------------------------

When('I switch the charge mode to {string}', async ({ page }, mode: string) => {
  await page.getByRole('radio', { name: mode, exact: true }).click();
});

When('I set the charge rate to {int} A', async ({ page }, amps: number) => {
  await page.getByRole('button', { name: 'Charge rate' }).click();
  await page.getByRole('slider', { name: 'Charge rate' }).fill(String(amps));
});

When('I set an energy limit of {int} kWh', async ({ page }, kwh: number) => {
  await page.getByRole('radio', { name: 'Energy', exact: true }).click();
  await page.getByRole('slider', { name: 'Energy' }).fill(String(kwh));
});

// ---- Setup wizard ------------------------------------------------------------

const WIZARD_STEPS = ['Welcome', 'Charger basics', 'Time', 'Password', 'Firmware', 'WiFi'];

Then('the setup wizard shows step {int} of {int} {string}', async ({ page }, n: number, of: number, title: string) => {
  await expect(page.getByRole('banner')).toContainText(`Step ${n} of ${of}`);
  await expect(page.getByRole('heading', { level: 1, name: title })).toBeVisible();
});

When('I continue to the {string} step', async ({ page }, title: string) => {
  const target = WIZARD_STEPS.indexOf(title);
  if (target < 0) throw new Error(`Unknown wizard step "${title}"`);
  await page.getByRole('button', { name: 'Next' }).click();
  await expect(page.getByRole('heading', { level: 1, name: title })).toBeVisible();
});

When('I set the maximum current to {int} A', async ({ page }, amps: number) => {
  await page.getByRole('slider').fill(String(amps));
});
