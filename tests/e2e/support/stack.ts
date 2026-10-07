import { ChildProcess, spawn } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, rmSync, createWriteStream } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { Device, Emulator, waitFor } from './clients';

/**
 * One firmware + emulator pair.
 *
 *   browser ──HTTP──▶ native firmware ──RAPI over a PTY──▶ emulator (EVSE + car)
 *
 * The native firmware is the real firmware compiled for Linux (`pio run -e
 * native_openevse`) and serves the real embedded web UI. The emulator is run
 * from a source checkout so it always has the test-control endpoints
 * (/api/test/*) this suite relies on.
 *
 * Environment:
 *   NATIVE_BINARY_PATH  firmware binary  (default ../../.pio/build/native_openevse/program)
 *   EMULATOR_DIR        emulator checkout (default ../../../OpenEVSE_Emulator)
 *   EMULATOR_PYTHON     python with the emulator's requirements installed (default python3)
 *   E2E_PORT_BASE       first TCP port; each worker uses a block of 10 (default 18000)
 *   E2E_VERBOSE=1       mirror firmware/emulator console output to the terminal
 */

const e2eRoot = path.resolve(__dirname, '..');
const repoRoot = path.resolve(e2eRoot, '..', '..');

function resolveBinary(): string {
  const candidates = [
    process.env.NATIVE_BINARY_PATH,
    path.join(repoRoot, '.pio/build/native_openevse/program'),
  ].filter((p): p is string => !!p);
  const found = candidates.find((p) => existsSync(p));
  if (!found) {
    throw new Error(
      `Native firmware binary not found (looked in: ${candidates.join(', ')}). ` +
        'Build it with `pio run -e native_openevse` or set NATIVE_BINARY_PATH.',
    );
  }
  return found;
}

function resolveEmulator(): string {
  const dir = path.resolve(process.env.EMULATOR_DIR ?? path.join(repoRoot, '..', 'OpenEVSE_Emulator'));
  if (!existsSync(path.join(dir, 'src', 'main.py'))) {
    throw new Error(`OpenEVSE emulator checkout not found at ${dir}. Set EMULATOR_DIR.`);
  }
  return dir;
}

async function stop(proc: ChildProcess | undefined) {
  if (!proc || proc.exitCode !== null) return;
  const exited = new Promise<void>((resolve) => proc.once('exit', () => resolve()));
  proc.kill('SIGTERM');
  const timer = setTimeout(() => proc.kill('SIGKILL'), 5000);
  await exited;
  clearTimeout(timer);
}

export class Stack {
  readonly emulator: Emulator;
  readonly device: Device;
  private emulatorProc?: ChildProcess;
  private nativeProc?: ChildProcess;
  private workdir?: string;
  private readonly ptyPath: string;
  private readonly logDir: string;

  private constructor(
    readonly workerIndex: number,
    private readonly nativePort: number,
    private readonly emulatorPort: number,
  ) {
    this.emulator = new Emulator(`http://127.0.0.1:${emulatorPort}`);
    this.device = new Device(`http://127.0.0.1:${nativePort}`);
    this.ptyPath = path.join(tmpdir(), `openevse_e2e_pty_${process.pid}_${workerIndex}`);
    this.logDir = path.join(e2eRoot, 'output', `worker-${workerIndex}`);
    mkdirSync(this.logDir, { recursive: true });
  }

  get deviceUrl() {
    return this.device.url;
  }

  static async start(workerIndex: number): Promise<Stack> {
    const base = Number(process.env.E2E_PORT_BASE ?? 18000) + workerIndex * 10;
    const stack = new Stack(workerIndex, base, base + 1);
    try {
      await stack.startEmulator();
      await stack.startNative();
    } catch (e) {
      await stack.stop();
      throw e;
    }
    return stack;
  }

  /** Clean slate for the next scenario: car, faults, time scale and device config. */
  async reset() {
    await stop(this.nativeProc);
    await this.emulator.reset();
    await this.startNative();
  }

  /** Restart only the firmware, keeping its stored config (for persistence checks). */
  async restartDevice() {
    await stop(this.nativeProc);
    await this.startNative(true);
  }

  async stop() {
    await stop(this.nativeProc);
    await stop(this.emulatorProc);
    if (this.workdir) rmSync(this.workdir, { recursive: true, force: true });
    rmSync(this.ptyPath, { force: true });
  }

  private pipeOutput(proc: ChildProcess, name: string) {
    const log = createWriteStream(path.join(this.logDir, `${name}.log`), { flags: 'a' });
    for (const stream of [proc.stdout, proc.stderr]) {
      stream?.on('data', (chunk) => {
        log.write(chunk);
        if (process.env.E2E_VERBOSE) process.stderr.write(`[${name}${this.workerIndex}] ${chunk}`);
      });
    }
    proc.once('exit', () => log.end());
  }

  private async startEmulator() {
    const dir = resolveEmulator();
    rmSync(this.ptyPath, { force: true });
    this.emulatorProc = spawn(
      process.env.EMULATOR_PYTHON ?? 'python3',
      [
        path.join(dir, 'src', 'main.py'),
        '--serial-mode', 'pty',
        '--serial-pty-path', this.ptyPath,
        '--web-host', '127.0.0.1',
        '--web-port', String(this.emulatorPort),
      ],
      { cwd: dir, env: { ...process.env, PYTHONUNBUFFERED: '1' } },
    );
    this.pipeOutput(this.emulatorProc, 'emulator');
    await waitFor('emulator HTTP API', async () => (await this.emulator.status()).evse, 30_000);
    await waitFor('emulator PTY', async () => existsSync(this.ptyPath), 10_000);
  }

  private async startNative(keepConfig = false) {
    if (!keepConfig || !this.workdir) {
      if (this.workdir) rmSync(this.workdir, { recursive: true, force: true });
      this.workdir = mkdtempSync(path.join(tmpdir(), 'openevse_e2e_fw_'));
    }
    this.nativeProc = spawn(
      resolveBinary(),
      [
        '--rapi-serial', this.ptyPath,
        '--set-config', `www_http_port=${this.nativePort}`,
        '--set-config', `hostname=openevse-e2e-${this.workerIndex}`,
      ],
      {
        cwd: this.workdir,
        env: { ...process.env, OPENEVSE_CHIP_ID: `1234567890ab${this.workerIndex.toString(16).padStart(4, '0')}` },
      },
    );
    this.pipeOutput(this.nativeProc, 'firmware');
    // HTTP being up does not mean the first RAPI exchange has happened yet.
    await waitFor(
      'firmware to report an EVSE state',
      async () => {
        const s = await this.device.status();
        return s.evse_connected === 1 && typeof s.state === 'number' ? s : undefined;
      },
      30_000,
    );
  }
}
