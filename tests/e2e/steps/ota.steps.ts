import { createHash, randomBytes } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { Given, When, Then, expect } from '../support/fixtures';
import { waitFor } from '../support/clients';

// The native build's Update class (EpoxyUpdate in EpoxyDuino) records what the
// OTA path handed to the updater. These steps read that record back.

const sha256 = (data: Buffer) => createHash('sha256').update(data).digest('hex');

/** A firmware-shaped image: starts with the ESP image magic, then random bytes. */
function makeImage(size: number): Buffer {
  const image = randomBytes(size);
  image[0] = 0xe9;
  return image;
}

function sinkFile(sinkDir: string, name: string): string {
  return path.join(sinkDir, name);
}

function readSinkJson(sinkDir: string, name: string): any {
  return JSON.parse(readFileSync(sinkFile(sinkDir, name), 'utf8'));
}

/** Wait until the device has finished (or failed) recording an update. */
async function waitForOtaEnd(sinkDir: string): Promise<any> {
  return waitFor(
    'the device to finish writing the update',
    async () => existsSync(sinkFile(sinkDir, 'ota_end.json')) && readSinkJson(sinkDir, 'ota_end.json'),
    60_000,
  );
}

Given('a firmware image of {int} bytes', async ({ world }, size: number) => {
  world.ota.image = makeImage(size);
});

Given(
  'the latest published firmware {string} from GitHub',
  async ({ world }, assetName: string) => {
    // Use the release GitHub itself publishes: its size and SHA-256 are the reference.
    // Unauthenticated API calls share a low rate limit on CI runners; use the
    // workflow token when there is one.
    const headers: Record<string, string> = { Accept: 'application/vnd.github+json' };
    if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;
    const res = await fetch('https://api.github.com/repos/OpenEVSE/openevse_esp32_firmware/releases/latest', { headers });
    expect(res.status, 'GitHub release lookup').toBe(200);
    const release = await res.json();
    const asset = release.assets.find((a: any) => a.name === assetName);
    expect(asset, `asset ${assetName} in ${release.tag_name}`).toBeTruthy();
    const digest: string = asset.digest ?? '';
    expect(digest.startsWith('sha256:'), 'GitHub publishes a SHA-256 digest for the asset').toBe(true);
    world.ota.release = {
      url: asset.browser_download_url,
      name: asset.name,
      size: asset.size,
      sha256: digest.slice('sha256:'.length),
    };
  },
);

When('I upload the image to the firmware update endpoint', async ({ world }) => {
  expect(world.ota.image, 'an image was set up').toBeTruthy();
  world.ota.response = await world.device.uploadFirmware(world.ota.image!, 'openevse_wifi_v1.bin');
});

When('I ask the device to update from {string}', async ({ world }, url: string) => {
  world.ota.response = await world.device.updateFromUrl(url);
});

When('I ask the device to update from that release asset', async ({ world }) => {
  expect(world.ota.release, 'a release was looked up').toBeTruthy();
  world.ota.response = await world.device.updateFromUrl(world.ota.release!.url);
});

Then('the device accepts the upload', async ({ world }) => {
  expect(world.ota.response?.status, world.ota.response?.body).toBe(200);
});

Then('the device refuses the update', async ({ world }) => {
  expect(world.ota.response?.status, world.ota.response?.body).not.toBe(200);
});

Then('the device starts the update', async ({ world }) => {
  expect(world.ota.response?.status, world.ota.response?.body).toBe(200);
});

Then('the updater was started from GitHub', async ({ stack }) => {
  const begin = await waitFor('the updater to be started', async () =>
    existsSync(sinkFile(stack.otaSinkDir, 'ota_begin.json')) ? readSinkJson(stack.otaSinkDir, 'ota_begin.json') : undefined,
  );
  // A download is recorded under the URL it came from; an upload has no source.
  // GitHub answers a release download with a redirect to its asset host, and the
  // updater records the URL it finally downloaded from, so either host is fine.
  expect(begin.source, 'source the updater was started from').toBeTruthy();
  const source = new URL(begin.source);
  expect(source.protocol, 'source uses HTTPS').toBe('https:');
  const fromGitHub = source.hostname === 'github.com' || source.hostname.endsWith('.githubusercontent.com');
  expect(fromGitHub, `source host ${source.hostname} is GitHub`).toBe(true);
});

Then('the device did not start writing an image', async ({ stack }) => {
  // A refused request never reaches the updater, so there is no event to wait for.
  // Give an accepted one a moment to show up before asserting it did not.
  await new Promise((resolve) => setTimeout(resolve, 3_000));
  expect(existsSync(sinkFile(stack.otaSinkDir, 'ota_begin.json'))).toBe(false);
});

Then('the updater was told to expect exactly the size GitHub published', async ({ world, stack }) => {
  const begin = await waitFor('the updater to be started', async () =>
    existsSync(sinkFile(stack.otaSinkDir, 'ota_begin.json')) ? readSinkJson(stack.otaSinkDir, 'ota_begin.json') : undefined,
  );
  expect(begin.begin_size, 'size passed to Update.begin()').toBe(world.ota.release!.size);
});

Then('the device received the image byte for byte', async ({ world, stack }) => {
  const end = await waitForOtaEnd(stack.otaSinkDir);
  expect(end.ok, 'the updater accepted the image').toBe(true);
  const received = readFileSync(sinkFile(stack.otaSinkDir, 'ota_image.bin'));
  if (world.ota.image) {
    expect(received.length, 'bytes received').toBe(world.ota.image.length);
    expect(sha256(received)).toBe(sha256(world.ota.image));
  } else {
    // A release download has no local copy; GitHub's published size is the reference
    expect(received.length, 'bytes received').toBe(world.ota.release!.size);
  }
});

Then('the image matches the SHA-256 GitHub publishes for the asset', async ({ world, stack }) => {
  const end = await waitForOtaEnd(stack.otaSinkDir);
  expect(end.ok).toBe(true);
  const received = readFileSync(sinkFile(stack.otaSinkDir, 'ota_image.bin'));
  expect(received.length, 'bytes received').toBe(world.ota.release!.size);
  expect(sha256(received)).toBe(world.ota.release!.sha256);
});

Then('the device reports the update as completed', async ({ stack }) => {
  const end = await waitForOtaEnd(stack.otaSinkDir);
  expect(end.ok).toBe(true);
});
