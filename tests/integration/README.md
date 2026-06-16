# OpenEVSE Integration Tests

Integration tests that spin up a real instance of the native firmware paired
with the OpenEVSE emulator, then exercise the HTTP API to verify basic
charging behaviour.

## Overview

Each test spawns one instance of the OpenEVSE emulator (Docker) and one
instance of the native firmware (host process), connects them over a virtual
serial port (socat PTY bridge), and then makes HTTP requests against the
firmware's web server.

Covered API endpoints:

| Endpoint         | Methods           | Tests                        |
|------------------|-------------------|------------------------------|
| `/status`        | GET, POST         | Status fields, vehicle data  |
| `/override`      | GET, POST, DELETE | Enable/disable charging      |
| `/config`        | GET, POST         | Read and update config       |
| `/claims`        | GET               | Active charge-manager claims |
| `/claims/target` | GET               | Target state/properties      |
| `/claims/{id}`   | POST, DELETE      | Claim lifecycle              |

## Prerequisites

### Local Development

1. **Native firmware build** – build once before running tests:
   ```bash
   pio run -e native_openevse
   ```

2. **Docker** – pull the emulator image:
   ```bash
   docker pull ghcr.io/jeremypoulter/openevse_emulator:latest
   ```

3. **socat** – TCP-to-PTY bridge utility:
   ```bash
   # Ubuntu/Debian
   sudo apt-get install socat

   # macOS
   brew install socat
   ```

4. **Python dependencies**:
   ```bash
   pip install -r tests/integration/requirements.txt
   ```

### CI Environment

All prerequisites are installed automatically by the GitHub Actions workflow
(`.github/workflows/integration_tests.yaml`). The native binary is downloaded
from the build artifacts produced by the main `build.yaml` workflow.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│ Test runner (pytest)                                 │
│                                                      │
│  HTTP requests ──► native firmware (port 8000)       │
│                         │                            │
│                     PTY /tmp/rapi_pty_0              │
│                         │                            │
│                    socat bridge                      │
│                         │                            │
│                    TCP port 8023                     │
│                         │                            │
│              emulator (Docker, port 8080)            │
└──────────────────────────────────────────────────────┘
```

- **Emulator (Docker)**: runs the OpenEVSE hardware emulator, exposes an HTTP
  control API on port 8080 and a RAPI TCP socket on port 8023.
- **socat**: bridges the TCP RAPI socket to a PTY on the host so the native
  firmware can open it like a real serial device.
- **Native firmware**: the compiled Linux binary, pointed at the PTY and
  configured to listen for HTTP on port 8000.

## Running Tests

```bash
# All integration tests
pytest tests/integration/ -v

# Single test class
pytest tests/integration/test_charging.py::TestOverride -v

# Single test
pytest tests/integration/test_charging.py::TestOverride::test_disable_charging -v

# With live output
pytest tests/integration/ -v -s
```

### Environment variables

| Variable             | Default                                    | Description               |
|----------------------|--------------------------------------------|---------------------------|
| `NATIVE_BINARY_PATH` | `.pio/build/native_openevse/program` | Path to native binary     |

## Test Timing

- **Setup per test**: ~10–20 s (spawn Docker container + wait for readiness)
- **Single test**: ~1–5 s
- **Full suite**: ~3–5 min

Each test is guarded by `@pytest.mark.timeout(60)` to prevent hangs.

## Debugging Failed Tests

### Port conflicts

```bash
# Check what is using the default ports
lsof -i :8000
lsof -i :8080
lsof -i :8023

# Kill the emulator container and clean up
docker rm -f emulator_0
rm -f /tmp/rapi_pty_0
```

### Docker issues

```bash
# View emulator logs
docker logs emulator_0

# Check running containers
docker ps
```

### Native firmware not starting

```bash
# Verify the binary exists
ls -l .pio/build/native_openevse/program

# Run manually
.pio/build/native_openevse/program \
  --rapi-serial /tmp/rapi_pty_0 \
  --set-config www_http_port=8000
```

## Adding New Tests

1. Add test methods to an existing class or create a new class in
   `test_charging.py` (or a new file).
2. Use the `evse_instance` fixture – it provides a running firmware instance:
   ```python
   def test_new_feature(self, evse_instance):
       response = requests.get(f"{evse_instance['native_url']}/some/endpoint")
       assert response.status_code == 200
   ```
3. Use `@pytest.mark.timeout(60)` on the class or individual test.
