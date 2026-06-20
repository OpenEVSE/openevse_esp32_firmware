"""
Pytest configuration and fixtures for OpenEVSE integration tests.

This module provides fixtures for spawning a paired instance of the
OpenEVSE emulator (Docker) and native firmware (host process) for
testing basic charging features via the HTTP API.
"""

import os
import subprocess
import time
import socket
import pytest
import requests
import docker
from pathlib import Path
from typing import Dict, Any


def is_port_available(port: int, retries: int = 10, delay: float = 1.0) -> bool:
    """
    Check if a port is available for binding.

    Retries multiple times in case of TIME_WAIT socket states from previous
    connections.

    Args:
        port: Port number to check
        retries: Number of retry attempts
        delay: Seconds to wait between retries

    Returns:
        True if port is available, False if timeout
    """
    for attempt in range(retries):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                s.bind(("127.0.0.1", port))
                return True
        except OSError:
            if attempt < retries - 1:
                time.sleep(delay)
    return False


def wait_for_http_ready(url: str, timeout: float = 30, poll_interval: float = 0.2) -> bool:
    """
    Poll an HTTP endpoint until it responds with 200 OK.

    Args:
        url: HTTP endpoint to poll (e.g., http://localhost:8000/status)
        timeout: Maximum seconds to wait
        poll_interval: Seconds between polls

    Returns:
        True if endpoint became ready, False if timeout
    """
    start = time.time()
    while time.time() - start < timeout:
        try:
            response = requests.get(url, timeout=2)
            if response.status_code == 200:
                return True
        except (requests.RequestException, ValueError):
            pass
        time.sleep(poll_interval)
    return False


def wait_for_evse_state(url: str, timeout: float = 30, poll_interval: float = 0.2) -> bool:
    """
    Poll GET /status until the EVSE state is no longer 0 (STARTING).

    The native firmware initialises with state=0 (OPENEVSE_STATE_STARTING) and
    only transitions to a real state (1=Not Connected, 2=Connected, …) once the
    first RAPI $GS response has been received from the emulator.  Tests that
    assert on the state field must wait for this transition to avoid false
    failures.

    Args:
        url: HTTP endpoint to poll (e.g., http://localhost:8000/status)
        timeout: Maximum seconds to wait
        poll_interval: Seconds between polls

    Returns:
        True if EVSE reached a non-starting state, False if timeout
    """
    start = time.time()
    while time.time() - start < timeout:
        try:
            response = requests.get(url, timeout=2)
            if response.status_code == 200:
                data = response.json()
                if data.get("state", 0) != 0:
                    return True
        except (requests.RequestException, ValueError):
            pass
        time.sleep(poll_interval)
    return False


def get_native_binary_path() -> Path:
    """
    Detect native firmware binary location.

    Priority:
    1. NATIVE_BINARY_PATH environment variable (set in CI)
    2. Local build output .pio/build/native_openevse/program (local development)

    Returns:
        Path to native binary

    Raises:
        FileNotFoundError if binary not found in any location
    """
    env_path = os.environ.get("NATIVE_BINARY_PATH")
    if env_path:
        path = Path(env_path).resolve()
        if path.exists():
            return path

    # Check local build location (relative to workspace root)
    for candidate in [
        Path.cwd() / ".pio" / "build" / "native_openevse" / "program",
        Path.cwd().parent.parent / ".pio" / "build" / "native_openevse" / "program",
    ]:
        if candidate.exists():
            return candidate

    raise FileNotFoundError(
        "Native firmware binary not found. Set NATIVE_BINARY_PATH or run "
        "'pio run -e native_openevse' to build locally."
    )


@pytest.fixture(scope="session")
def docker_client():
    """
    Get Docker client for managing emulator containers.

    Skips all tests if Docker is not available.
    """
    try:
        client = docker.from_env()
        client.ping()
        return client
    except Exception as e:
        pytest.skip(f"Docker not available: {e}")


@pytest.fixture(scope="session")
def emulator_image(docker_client):
    """
    Ensure the emulator Docker image is available, pulling if needed.

    Returns:
        Docker image object for ghcr.io/jeremypoulter/openevse_emulator:latest
    """
    image_name = "ghcr.io/jeremypoulter/openevse_emulator:latest"
    try:
        return docker_client.images.get(image_name)
    except docker.errors.ImageNotFound:
        try:
            return docker_client.images.pull(image_name)
        except Exception as e:
            pytest.skip(f"Failed to pull emulator image: {e}")


def _cleanup_instance(port_offset: int, docker_client) -> None:
    """Kill any containers/processes using ports for the given offset."""
    emulator_port = 8080 + port_offset
    emulator_rapi_port = 8023 + port_offset
    native_port = 8000 + port_offset
    pty_path = f"/tmp/rapi_pty_{port_offset}"

    try:
        existing = docker_client.containers.list(
            all=True, filters={"name": f"emulator_{port_offset}"}
        )
        for container in existing:
            try:
                container.stop(timeout=2)
                container.remove(force=True)
            except Exception:
                pass
    except Exception:
        pass

    for port in [native_port, emulator_port, emulator_rapi_port]:
        try:
            subprocess.run(
                f"fuser -k {port}/tcp 2>/dev/null || true",
                shell=True,
                timeout=5,
            )
        except Exception:
            pass

    try:
        subprocess.run(
            f"pkill -f 'socat.*{pty_path}' 2>/dev/null || true",
            shell=True,
            timeout=5,
        )
    except Exception:
        pass

    try:
        if os.path.exists(pty_path):
            os.remove(pty_path)
    except Exception:
        pass

    time.sleep(1)


@pytest.fixture(scope="session")
def evse_instance(docker_client, emulator_image, tmp_path_factory):
    """
    Spawn a paired emulator + native firmware instance shared by the whole
    test session.

    The container, native firmware process and socat bridge are expensive to
    start, so they are created once per session.  Per-test isolation is
    provided by the ``reset_evse_state`` autouse fixture, which clears any
    override/config mutations between tests.

    Yields a dict with:
        - emulator_url: base URL for the emulator (http://localhost:808X)
        - native_url:   base URL for the native firmware (http://localhost:800X)
        - emulator_port / native_port / pty_path: raw values

    Both processes are fully stopped after the test.
    """
    native_binary = get_native_binary_path()
    port_offset = 0
    emulator_port = 8080 + port_offset
    emulator_rapi_port = 8023 + port_offset
    native_port = 8000 + port_offset
    pty_path = f"/tmp/rapi_pty_{port_offset}"

    _cleanup_instance(port_offset, docker_client)

    for port, desc in [
        (emulator_port, "emulator"),
        (emulator_rapi_port, "emulator RAPI"),
        (native_port, "native"),
    ]:
        if not is_port_available(port):
            pytest.fail(
                f"{desc} port {port} still in use after cleanup. "
                f"Run: fuser -k {port}/tcp && sleep 5"
            )

    # Start emulator container
    container = None
    socat_process = None
    native_process = None
    try:
        container = docker_client.containers.run(
            emulator_image.id,
            detach=True,
            remove=True,
            name=f"emulator_{port_offset}",
            ports={
                "8080/tcp": emulator_port,
                "8023/tcp": emulator_rapi_port,
            },
            environment={
                "WEB_PORT": "8080",
                "SERIAL_MODE": "tcp",
                "SERIAL_TCP_PORT": "8023",
            },
        )

        if not wait_for_http_ready(f"http://localhost:{emulator_port}/api/status", timeout=30):
            pytest.fail(f"Emulator did not become ready on port {emulator_port}")

        # Create socat TCP-to-PTY bridge
        socat_process = subprocess.Popen(
            [
                "socat",
                f"PTY,link={pty_path},rawer,wait-slave",
                f"TCP:localhost:{emulator_rapi_port}",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        for _ in range(50):
            if os.path.exists(pty_path):
                break
            time.sleep(0.1)
        else:
            pytest.fail(f"PTY not created at {pty_path} after 5 seconds")

        # Start native firmware
        instance_workdir = tmp_path_factory.mktemp("native_instance")

        native_process = subprocess.Popen(
            [
                str(native_binary),
                "--rapi-serial",
                pty_path,
                "--set-config",
                f"www_http_port={native_port}",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=str(instance_workdir),
        )

        if not wait_for_http_ready(f"http://localhost:{native_port}/config", timeout=30):
            pytest.fail(f"Native firmware did not become ready on port {native_port}")

        # Wait for EVSE state to transition out of STARTING (0) – the HTTP
        # server comes up before the first RAPI response arrives, so tests that
        # inspect /status["state"] would see 0 if we don't wait here.
        if not wait_for_evse_state(f"http://localhost:{native_port}/status", timeout=30):
            pytest.fail(
                f"EVSE did not reach a valid state on port {native_port} "
                f"(still in STARTING state after 30s – RAPI communication may have failed)"
            )

        yield {
            "emulator_port": emulator_port,
            "native_port": native_port,
            "pty_path": pty_path,
            "emulator_url": f"http://localhost:{emulator_port}",
            "native_url": f"http://localhost:{native_port}",
        }

    finally:
        if socat_process is not None:
            try:
                socat_process.terminate()
                socat_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                socat_process.kill()
            except Exception:
                pass

        if container is not None:
            try:
                container.stop(timeout=2)
            except Exception:
                pass

        if native_process is not None:
            try:
                native_process.terminate()
                native_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                native_process.kill()
            except Exception:
                pass


@pytest.fixture(scope="session")
def evse_baseline_config(evse_instance):
    """Capture the firmware's initial /config so it can be restored per test."""
    try:
        return requests.get(
            f"{evse_instance['native_url']}/config", timeout=5
        ).json()
    except (requests.RequestException, ValueError):
        return {}


@pytest.fixture(autouse=True)
def reset_evse_state(evse_instance, evse_baseline_config):
    """
    Reset mutable EVSE state *after* each test so the shared session instance
    behaves like a fresh one for the next test.

    Cleanup runs in teardown (rather than setup) so each test exercises the
    same path it would on a dedicated instance, without a reset request racing
    the test's own override/config writes.  Config is only rewritten when it
    actually differs from the captured baseline, to avoid coalescing rapid
    back-to-back config writes.
    """
    native_url = evse_instance["native_url"]

    yield

    # Clear any manual override left behind by the test.
    try:
        requests.delete(f"{native_url}/override", timeout=5)
    except requests.RequestException:
        pass

    # Restore mutated config fields to their baseline values, but only if they
    # changed (avoids unnecessary writes that the firmware may coalesce).
    baseline_current = evse_baseline_config.get("max_current_soft")
    if baseline_current is not None:
        try:
            current = requests.get(f"{native_url}/config", timeout=5).json()
            if current.get("max_current_soft") != baseline_current:
                requests.post(
                    f"{native_url}/config",
                    json={"max_current_soft": baseline_current},
                    timeout=5,
                )
        except (requests.RequestException, ValueError):
            pass
