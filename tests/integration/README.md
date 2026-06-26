# Load Sharing Peer Management Integration Tests

Integration tests for the load sharing peer management feature, validating peer discovery and management via REST API endpoints.

## Overview

These tests spawn multiple paired instances of the OpenEVSE emulator and native firmware, verify mDNS peer discovery works correctly, and validate all peer management REST API endpoints:

- `GET /loadsharing/peers` - List discovered and configured peers
- `POST /loadsharing/peers` - Add peer to configured group
- `DELETE /loadsharing/peers/{host}` - Remove peer from configured group
- `POST /loadsharing/discover` - Trigger immediate mDNS discovery

Tests are parametrized to run with 2, 3, and 4 paired instances to verify scaling.

## Prerequisites

### Local Development

1. **Native firmware build** - Build once before running tests:
   ```bash
   cd ../..
   pio run -e native
   ```

2. **Docker image** - Pull the emulator image:
   ```bash
   docker pull ghcr.io/jeremypoulter/openevse_emulator:latest
   ```

3. **mDNS daemon** - Avahi must be installed and running:
   ```bash
   # Ubuntu/Debian
   sudo apt-get install avahi-daemon
   sudo systemctl start avahi-daemon
   
   # macOS - mDNS is built-in (Bonjour)
   ```

4. **socat** - TCP-to-PTY bridge utility:
   ```bash
   # Ubuntu/Debian
   sudo apt-get install socat
   
   # macOS
   brew install socat
   ```

5. **Dependencies** - Install test requirements:
   ```bash
   pip install -r requirements.txt
   ```

### CI Environment

- Avahi is already installed as part of native firmware build prereqs
- Docker is available
- socat installed via apt
- Native binary downloaded from build artifacts

## Architecture

Each test spawns paired instances:

1. **Emulator (Docker)**: Runs OpenEVSE emulator exposing:
   - HTTP API on port 8080 + offset
   - RAPI TCP socket on port 8023 + offset

2. **Native Firmware (Host)**: Runs compiled native binary exposing:
   - HTTP API on port 8000 + offset
   - Connects to RAPI via PTY

3. **PTY Bridge (socat)**: Creates TCP-to-PTY bridge on host:
   - Listens on PTY at `/tmp/rapi_pty_N`
   - Connects to emulator's TCP RAPI port
   - Enables native firmware to communicate with containerized emulator

This architecture solves the `/dev/pts` namespace isolation issue where Docker containers' PTY devices aren't accessible from the host.

## Running Tests

### Locally (Recommended for Development)

```bash
# Run all tests
pytest -v

# Run with specific verbosity
pytest -v --tb=short

# Run specific test
pytest -v test_loadsharing_peer_management.py::TestPeerManagement::test_add_peer_manual

# Run tests for specific instance count
pytest -v -k "test_peer_discovery_mdns[2]"

# Run with live output (no capture)
pytest -v -s
```

### Parallel Test Execution

Tests can't run in parallel because they use fixed port ranges (8000-8003, 8080-8083). Use sequential execution:

```bash
pytest -v -n auto  # Will be slower due to port contention
pytest -v          # Sequential - faster and more reliable
```

## Debugging Failed Tests

### Port Conflicts

If tests fail with "Port already in use" errors:

```bash
# Check what's using the ports
lsof -i :8000-8003
lsof -i :8080-8083

# Kill containers and clean up
docker ps -q --filter "name=emulator_" | xargs -r docker stop
docker ps -q --filter "name=emulator_" | xargs -r docker kill
rm -f /tmp/rapi_pty_*
```

### mDNS Issues

If mDNS discovery fails (tests timeout):

```bash
# Check if Avahi is running
ps aux | grep avahi-daemon

# Check mDNS resolution manually
avahi-browse -a -r  # List all mDNS services

# Restart Avahi
sudo systemctl restart avahi-daemon

# On macOS, mDNS should work automatically
```

### Docker Container Issues

If containers fail to start:

```bash
# Check container logs
docker logs emulator_0 emulator_1

# Check for permission issues
docker ps  # Should show running containers
docker inspect emulator_0  # Detailed container info

# Clean up stuck containers
docker rm -f emulator_0 emulator_1 emulator_2 emulator_3
```

### PTY Issues

If PTY creation fails:

```bash
# Check /tmp permissions
ls -la /tmp | grep rapi_pty

# Clean up old PTY files
rm -f /tmp/rapi_pty_*

# Verify PTY support
ls /dev/pts/
```

## Test Structure

Tests are organized into two classes:

### TestPeerManagement

Core peer management endpoint tests:
- Initial state (empty peer list)
- Discovery trigger
- mDNS multi-instance discovery (2, 3, 4 instances)
- Manual peer addition
- Duplicate peer rejection
- Peer deletion
- Deletion of nonexistent peer
- Discovered peer joined status

### TestResponseStructure

API compliance tests:
- Response structure validation
- Error response format

## Environment Variables

### Local Development

```bash
# Override native binary location (default: .pio/build/native/program)
export NATIVE_BINARY_PATH=/path/to/native/binary

# Run tests
pytest -v
```

### CI (GitHub Actions)

```bash
# Download native artifact from build job
NATIVE_BINARY_PATH=./native_program pytest -v
```

## Test Timing

- **Setup per test**: ~5-10 seconds (spawn containers + wait for readiness)
- **Single test**: ~1-3 seconds
- **mDNS discovery test**: ~5-10 seconds (includes 3s mDNS wait)
- **Full suite (all parametrizations)**: ~60-90 seconds

Each test has `@pytest.mark.timeout(60)` to prevent hangs.

## GitHub Actions Workflow

Tests run in CI after successful build via `integration_tests.yaml`:

```yaml
needs: build  # Waits for build.yaml to complete

strategy:
  matrix:
    num_instances: [2, 3, 4]  # Tests each configuration

steps:
  - Download native artifact
  - Pull emulator Docker image
  - Run: pytest tests/integration/ -v
```

Test results are published to the PR via `EnricoMi/publish-unit-test-result-action`.

## Common Issues and Solutions

| Issue | Cause | Solution |
|-------|-------|----------|
| "Port already in use" | Prior test or service using 8000-8003 / 8080-8083 | `docker ps -q --filter "name=emulator_" \| xargs -r docker stop && rm -f /tmp/rapi_pty_*` |
| "mDNS not available" | Avahi not installed | `sudo apt-get install avahi-daemon` |
| "Failed to pull image" | Network unreachable | Check internet connection, or `docker pull ghcr.io/jeremypoulter/openevse_emulator:latest` manually |
| "Docker not available" | Docker daemon not running | `sudo systemctl start docker` or ensure Docker Desktop is running |
| "PTY permission denied" | /tmp permissions too restrictive | `chmod 777 /tmp` or run tests with appropriate permissions |
| "Peer discovery times out" | mDNS slower than expected | Increase timeout in conftest.py `wait_for_http_ready()` |
| "Binary not found" | Native firmware not built | `pio run -e native` from ESP32_WiFi_V3.x root |

## Expected Test Results

All tests should pass with healthy system:

```
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_peers_endpoint_initial_state PASSED
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_discover_trigger PASSED
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_peer_discovery_mdns[2] PASSED
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_peer_discovery_mdns[3] PASSED
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_peer_discovery_mdns[4] PASSED
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_add_peer_manual PASSED
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_add_peer_duplicate_rejection PASSED
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_delete_peer PASSED
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_delete_nonexistent_peer PASSED
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_discovered_peers_joined_status[2] PASSED
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_discovered_peers_joined_status[3] PASSED
tests/integration/test_loadsharing_peer_management.py::TestPeerManagement::test_discovered_peers_joined_status[4] PASSED
tests/integration/test_loadsharing_peer_management.py::TestResponseStructure::test_peers_response_structure PASSED
tests/integration/test_loadsharing_peer_management.py::TestResponseStructure::test_error_response_structure PASSED

====== 14 passed in 85.23s ======
```

## Adding New Tests

To add tests for additional load sharing features or new endpoints:

1. Add test methods to existing classes or create new test classes
2. Use the `instance_pair` or `multi_instance_group` fixtures
3. Make HTTP requests using `requests` library
4. Assert response codes, JSON structure, and field values
5. Use descriptive docstrings explaining what's being tested

Example:

```python
def test_new_feature(self, multi_instance_group):
    """Test: New feature description."""
    pairs = multi_instance_group(2)
    
    # Make request
    response = requests.post(
        f"{pairs[0]['native_url']}/some/endpoint",
        json={"param": "value"}
    )
    
    # Assert expectations
    assert response.status_code == 200
    assert response.json().get("result") == "expected"
```

## CI Artifact Path

In GitHub Actions workflow `integration_tests.yaml`:

```yaml
- name: Download artifact
  uses: actions/download-artifact@v6
  with:
    name: native.bin
    path: ./

- name: Make executable
  run: chmod +x native_program

- name: Run tests
  env:
    NATIVE_BINARY_PATH: ./native_program
  run: pytest tests/integration/ -v
```
