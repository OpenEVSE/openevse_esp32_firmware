"""
Pytest configuration and fixtures for load sharing integration tests.

This module provides fixtures for spawning and managing paired instances of the
OpenEVSE emulator and native firmware for testing load sharing peer management endpoints.
"""

import os
import subprocess
import time
import socket
import shutil
import pytest
import requests
import docker
from pathlib import Path
from typing import Dict, List, Any
from urllib.parse import quote


def pytest_configure(config):
    """Initialize session-level counter for unique port offsets."""
    config.port_offset_counter = 0


def is_port_available(port: int, retries: int = 10, delay: float = 1.0) -> bool:
    """
    Check if a port is available for binding.

    Retries multiple times in case of TIME_WAIT socket states from previous connections.

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
        except OSError as e:
            if attempt < retries - 1:
                time.sleep(delay)
                continue
    return False


def wait_for_http_ready(url: str, timeout: float = 30, poll_interval: float = 0.5) -> bool:
    """
    Poll an HTTP endpoint until it responds with 200 OK.

    Args:
        url: HTTP endpoint to poll (e.g., http://localhost:8080/api/status)
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
        except requests.RequestException:
            pass
        time.sleep(poll_interval)
    return False


def get_pty_name(pty_path: str) -> str:
    """
    Extract the PTY name from a full PTY path for easy identification.

    Args:
        pty_path: Path like /tmp/rapi_pty_0

    Returns:
        PTY name for logging
    """
    return os.path.basename(pty_path)


@pytest.fixture(scope="session")
def check_mdns_support():
    """
    Verify mDNS daemon is available.

    Will skip all tests if mDNS tools are not available or not running.
    Performs initial cleanup before running any tests.
    """
    # Cleanup stuck containers and PTY files from previous runs
    print("\n[Setup] Cleaning up stuck containers from previous test runs...")
    try:
        subprocess.run(
            "docker ps -q --filter 'name=emulator_' | xargs -r docker rm -f 2>/dev/null || true",
            shell=True,
            timeout=10
        )
    except Exception as e:
        print(f"Warning: Could not clean up Docker: {e}")

    try:
        subprocess.run("pkill -f 'socat.*rapi_pty' 2>/dev/null || true", shell=True, timeout=5)
    except Exception as e:
        print(f"Warning: Could not clean up socat processes: {e}")

    try:
        subprocess.run("rm -f /tmp/rapi_pty_* 2>/dev/null || true", shell=True, timeout=5)
    except Exception as e:
        print(f"Warning: Could not clean up PTY files: {e}")

    # Now check for mDNS support
    result = shutil.which("avahi-browse")
    if not result:
        result = shutil.which("dns-sd")

    if not result:
        pytest.skip("mDNS tools not available (avahi-browse or dns-sd)")


@pytest.fixture(scope="session")
def docker_client(check_mdns_support):
    """
    Get Docker client for managing emulator containers.

    Depends on check_mdns_support to ensure environment is ready.
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
    Ensure emulator Docker image is available, pulling if needed.

    Returns:
        Docker image object for ghcr.io/jeremypoulter/openevse_emulator:latest
    """
    image_name = "ghcr.io/jeremypoulter/openevse_emulator:latest"
    try:
        image = docker_client.images.get(image_name)
        return image
    except docker.errors.ImageNotFound:
        pytest.skip(f"Pulling {image_name}...")
        try:
            image = docker_client.images.pull(image_name)
            return image
        except Exception as e:
            pytest.skip(f"Failed to pull emulator image: {e}")


def get_native_binary_path() -> Path:
    """
    Detect native firmware binary location.

    Priority:
    1. NATIVE_BINARY_PATH environment variable (set in CI)
    2. Local build output .pio/build/native/program (local development)

    Returns:
        Path to native binary

    Raises:
        FileNotFoundError if binary not found in any location
    """
    # Check environment variable first (used in CI)
    env_path = os.environ.get("NATIVE_BINARY_PATH")
    if env_path:
        path = Path(env_path)
        if path.exists():
            return path

    # Check local build location (relative to workspace root)
    local_path = Path.cwd().parent.parent / ".pio" / "build" / "native" / "program"
    if local_path.exists():
        return local_path

    # Check if we're already in the right directory
    local_path = Path.cwd() / ".pio" / "build" / "native" / "program"
    if local_path.exists():
        return local_path

    raise FileNotFoundError(
        "Native firmware binary not found. Set NATIVE_BINARY_PATH or run "
        "'pio run -e native' to build locally."
    )


@pytest.fixture
def instance_pair(docker_client, emulator_image, tmp_path, request):
    """
    Factory fixture to create a paired emulator+native instance.

    Usage:
        def test_something(instance_pair):
            pair = instance_pair(port_offset=0)
            emulator_port = pair["emulator_port"]
            native_port = pair["native_port"]

    Args:
        port_offset: Starting offset for ports (0, 1, 2, etc.)
                     Emulator on 8080+offset, Native on 8000+offset

    Returns:
        Dict with keys:
            - emulator_port: HTTP port for emulator web UI
            - native_port: HTTP port for native firmware web server
            - pty_path: PTY path for serial communication
            - emulator_container: Docker container object
            - native_process: subprocess.Popen object
    """
    native_binary = get_native_binary_path()
    containers = []
    processes = []
    socat_processes = []

    def cleanup_conflicting_resources(port_offset: int):
        """Kill any containers/processes using conflicting ports/PTYs."""
        emulator_port = 8080 + port_offset
        emulator_rapi_port = 8023 + port_offset
        native_port = 8000 + port_offset
        pty_path = f"/tmp/rapi_pty_{port_offset}"

        # Try to kill any containers with conflicting names
        try:
            existing = docker_client.containers.list(all=True, filters={"name": f"emulator_{port_offset}"})
            for container in existing:
                try:
                    container.stop(timeout=2)
                    container.remove(force=True)
                except Exception:
                    pass
        except Exception:
            pass

        # Try to kill processes using the ports
        for port in [native_port, emulator_port, emulator_rapi_port]:
            try:
                subprocess.run(
                    f"fuser -k {port}/tcp 2>/dev/null || true",
                    shell=True,
                    timeout=5
                )
            except Exception:
                pass

        # Kill any socat processes for this offset
        try:
            subprocess.run(
                f"pkill -f 'socat.*{pty_path}' 2>/dev/null || true",
                shell=True,
                timeout=5
            )
        except Exception:
            pass

        # Clean up PTY symlink if it exists
        try:
            if os.path.exists(pty_path):
                os.remove(pty_path)
        except Exception:
            pass

        # Wait a bit for resources to be released
        time.sleep(1)

    def create_pair(port_offset: int = 0) -> Dict[str, Any]:
        """Spawn one emulator+native pair."""
        emulator_port = 8080 + port_offset
        emulator_rapi_port = 8023 + port_offset
        native_port = 8000 + port_offset
        pty_path = f"/tmp/rapi_pty_{port_offset}"

        # Clean up any conflicting resources
        cleanup_conflicting_resources(port_offset)

        # Check ports are available (with retries)
        for port, desc in [(emulator_port, "emulator"), (emulator_rapi_port, "emulator RAPI"), (native_port, "native")]:
            if not is_port_available(port):
                pytest.fail(
                    f"{desc.capitalize()} port {port} still in use after cleanup. "
                    f"Manual cleanup: fuser -k {port}/tcp && sleep 5"
                )

        # Start emulator container with TCP RAPI port exposed
        container_name = f"emulator_{port_offset}"
        try:
            container = docker_client.containers.run(
                emulator_image.id,
                detach=True,
                remove=True,
                name=container_name,
                ports={
                    f"8080/tcp": emulator_port,
                    f"8023/tcp": emulator_rapi_port  # Map RAPI TCP port
                },
                environment={
                    "WEB_PORT": "8080",
                    "SERIAL_MODE": "tcp",  # Use TCP mode instead of PTY
                    "SERIAL_TCP_PORT": "8023",
                },
            )
            containers.append(container)
        except docker.errors.APIError as e:
            pytest.fail(f"Failed to start emulator container: {e}")

        # Wait for emulator readiness
        emulator_url = f"http://localhost:{emulator_port}/api/status"
        if not wait_for_http_ready(emulator_url, timeout=30):
            try:
                container.stop()
            except:
                pass
            pytest.fail(f"Emulator did not become ready at {emulator_url}")

        # Create socat bridge: TCP (from emulator) -> PTY (for native firmware)
        # This creates a real PTY on the host and bridges it to emulator's TCP RAPI port
        try:
            socat_process = subprocess.Popen(
                [
                    "socat",
                    f"PTY,link={pty_path},rawer,wait-slave",
                    f"TCP:localhost:{emulator_rapi_port}",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            socat_processes.append(socat_process)

            # Wait for PTY to be created
            for _ in range(50):  # 5 seconds max
                if os.path.exists(pty_path):
                    break
                time.sleep(0.1)
            else:
                raise Exception(f"PTY not created at {pty_path} after 5 seconds")

        except Exception as e:
            try:
                container.stop()
            except:
                pass
            pytest.fail(f"Failed to create socat PTY bridge: {e}")

        # Create unique working directory for this instance's persistent storage
        instance_workdir = tmp_path / f"native_instance_{port_offset}"
        instance_workdir.mkdir(parents=True, exist_ok=True)

        # Start native firmware in its own working directory
        # This prevents NVS persistence conflicts between tests
        try:
            process = subprocess.Popen(
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
                cwd=str(instance_workdir),  # Each instance gets unique storage
            )
            processes.append(process)
        except Exception as e:
            try:
                container.stop()
            except:
                pass
            pytest.fail(f"Failed to start native firmware: {e}")

        # Wait for native readiness
        native_url = f"http://localhost:{native_port}/config"
        if not wait_for_http_ready(native_url, timeout=30):
            try:
                container.stop()
            except:
                pass
            process.terminate()
            pytest.fail(f"Native firmware did not become ready at {native_url}")

        return {
            "emulator_port": emulator_port,
            "native_port": native_port,
            "pty_path": pty_path,
            "emulator_container": container,
            "native_process": process,
            "emulator_url": f"http://localhost:{emulator_port}",
            "native_url": f"http://localhost:{native_port}",
        }

    # Create and return the factory function
    yield create_pair

    # Cleanup: stop all socat bridges first (they depend on container ports)
    for socat_proc in socat_processes:
        try:
            socat_proc.terminate()
            socat_proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            socat_proc.kill()
        except Exception as e:
            print(f"Warning: Failed to stop socat: {e}")

    # Cleanup: stop all containers and processes
    for container in containers:
        try:
            container.stop(timeout=2)
        except Exception as e:
            print(f"Warning: Failed to stop container: {e}")

    for process in processes:
        try:
            process.terminate()
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
        except Exception as e:
            print(f"Warning: Failed to terminate process: {e}")

    # Extra cleanup delay to ensure ports are released
    time.sleep(0.5)


@pytest.fixture
def multi_instance_group(instance_pair, request):
    """
    Factory fixture to create multiple paired instances.

    Usage:
        @pytest.mark.parametrize("num_instances", [2, 3, 4])
        def test_something(multi_instance_group, num_instances):
            pairs = multi_instance_group(num_instances)
            # pairs is a list of instance dictionaries

    Args:
        num_instances: Number of emulator+native pairs to spawn

    Returns:
        List of instance pair dictionaries
    """
    def create_group(num_instances: int) -> List[Dict[str, Any]]:
        """Spawn multiple instance pairs."""
        if num_instances < 2 or num_instances > 4:
            raise ValueError("num_instances must be between 2 and 4")

        pairs = []
        for offset in range(num_instances):
            pair = instance_pair(port_offset=offset)
            pairs.append(pair)

        return pairs

    return create_group


def pytest_configure(config):
    """Initialize session-level counter for unique port offsets."""
    config.port_offset_counter = 0


@pytest.fixture
def unique_port_offset(request):
    """
    Assign unique port offset for each test via session counter.

    Uses a session-scoped counter to ensure no port collisions.
    Offset cycles 0-9 to support up to 10 concurrent instances.

    Returns:
        An integer offset that's guaranteed unique per test (0-9)
    """
    offset = request.config.port_offset_counter % 10
    request.config.port_offset_counter += 1
    return offset


@pytest.fixture
def peer_hostname_factory(unique_port_offset):
    """
    Factory to create unique peer hostnames based on test offset.

    This ensures that peer hostnames don't collide with previously
    persisted peers in the firmware's NVS storage.

    Usage:
        def test_something(peer_hostname_factory):
            peer1 = peer_hostname_factory("test")
            peer2 = peer_hostname_factory("another")
            # peer1 = "openevse-test-5.local" (if offset=5)
            # peer2 = "openevse-another-5.local"

    Args:
        prefix: Short prefix for the hostname (e.g., "test", "duplicate")

    Returns:
        Unique hostname in format: openevse-{prefix}-{offset}.local
    """
    def create_hostname(prefix: str) -> str:
        return f"openevse-{prefix}-{unique_port_offset}.local"
    return create_hostname


@pytest.fixture
def instance_pair_auto(instance_pair, unique_port_offset):
    """
    Convenience wrapper that creates instances with auto-assigned unique ports.

    Usage:
        def test_something(instance_pair_auto):
            pair = instance_pair_auto()  # Uses auto-assigned unique offset

    Returns:
        Same dict as instance_pair but with auto-assigned, test-unique port offset
    """
    def auto_create():
        return instance_pair(port_offset=unique_port_offset)

    return auto_create
