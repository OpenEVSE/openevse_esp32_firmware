/** Thin REST clients for the two things the tests talk to besides the browser. */

async function request(url: string, init?: RequestInit): Promise<any> {
  const res = await fetch(url, init);
  const text = await res.text();
  if (!res.ok) {
    throw new Error(`${init?.method ?? 'GET'} ${url} -> ${res.status}: ${text}`);
  }
  try {
    return JSON.parse(text);
  } catch {
    return text;
  }
}

const json = (method: string, body?: unknown): RequestInit => ({
  method,
  headers: { 'Content-Type': 'application/json' },
  body: body === undefined ? undefined : JSON.stringify(body),
});

/** The OpenEVSE emulator: the "EVSE hardware" and the car, driven from the test. */
export class Emulator {
  constructor(readonly url: string) {}

  status() {
    return request(`${this.url}/api/status`);
  }
  reset() {
    return request(`${this.url}/api/test/reset`, json('POST'));
  }
  setTimeScale(scale: number) {
    return request(`${this.url}/api/test/time_scale`, json('POST', { scale }));
  }
  connectVehicle() {
    return request(`${this.url}/api/ev/connect`, json('POST'));
  }
  disconnectVehicle() {
    return request(`${this.url}/api/ev/disconnect`, json('POST'));
  }
  requestCharge() {
    return request(`${this.url}/api/ev/request_charge`, json('POST'));
  }
  stopRequestingCharge() {
    return request(`${this.url}/api/ev/stop_charge`, json('POST'));
  }
  setSoc(soc: number) {
    return request(`${this.url}/api/ev/soc`, json('POST', { soc }));
  }
  setChargeLimit(chargeLimitSoc: number) {
    return request(`${this.url}/api/ev/charge_limit`, json('POST', { charge_limit_soc: chargeLimitSoc }));
  }
  triggerFault(error: string) {
    return request(`${this.url}/api/errors/trigger`, json('POST', { error }));
  }
  clearFaults() {
    return request(`${this.url}/api/errors/clear`, json('POST'));
  }
}

/** The firmware's own HTTP API, used to assert backend state independently of the UI. */
export class Device {
  constructor(readonly url: string) {}

  status() {
    return request(`${this.url}/status`);
  }
  config() {
    return request(`${this.url}/config`);
  }
  setConfig(values: Record<string, unknown>) {
    return request(`${this.url}/config`, json('POST', values));
  }
  override() {
    return request(`${this.url}/override`);
  }
  setOverride(values: Record<string, unknown>) {
    return request(`${this.url}/override`, json('POST', values));
  }
  clearOverride() {
    return request(`${this.url}/override`, json('DELETE'));
  }
  limit() {
    return request(`${this.url}/limit`);
  }
  setLimit(values: Record<string, unknown>) {
    return request(`${this.url}/limit`, json('POST', values));
  }
}

/** Poll until `check` returns a truthy value or the timeout elapses. */
export async function waitFor<T>(
  description: string,
  check: () => Promise<T | undefined | false | null>,
  timeoutMs = 20_000,
  intervalMs = 250,
): Promise<T> {
  const deadline = Date.now() + timeoutMs;
  let last: unknown;
  while (Date.now() < deadline) {
    try {
      const value = await check();
      if (value) return value;
    } catch (e) {
      last = e;
    }
    await new Promise((r) => setTimeout(r, intervalMs));
  }
  throw new Error(`Timed out waiting for ${description}${last ? ` (last error: ${last})` : ''}`);
}
