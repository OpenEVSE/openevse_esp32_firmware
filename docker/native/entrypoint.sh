#!/usr/bin/env sh
set -eu

RAPI_PTY_PATH="${RAPI_PTY_PATH:-/tmp/rapi_pty}"
EMULATOR_HOST="${EMULATOR_HOST:-localhost}"
EMULATOR_PORT="${EMULATOR_PORT:-8023}"

cleanup() {
  for pid in "${NATIVE_PID:-}" "${SOCAT_PID:-}"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  for pid in "${NATIVE_PID:-}" "${SOCAT_PID:-}"; do
    if [ -n "$pid" ]; then
      wait "$pid" 2>/dev/null || true
    fi
  done
}

trap cleanup INT TERM EXIT

echo "Starting serial bridge: ${EMULATOR_HOST}:${EMULATOR_PORT} -> ${RAPI_PTY_PATH}"
socat -d -d \
  PTY,link="${RAPI_PTY_PATH}",raw,echo=0,waitslave \
  TCP:"${EMULATOR_HOST}:${EMULATOR_PORT}" &
SOCAT_PID=$!

# Wait for socat to create the PTY and connect
echo "Waiting for PTY device ${RAPI_PTY_PATH}..."
timeout=30
while [ ! -e "${RAPI_PTY_PATH}" ]; do
  if ! kill -0 "$SOCAT_PID" 2>/dev/null; then
    echo "ERROR: socat exited before creating PTY"
    exit 1
  fi
  timeout=$((timeout - 1))
  if [ "$timeout" -le 0 ]; then
    echo "ERROR: timed out waiting for PTY device"
    exit 1
  fi
  sleep 1
done
echo "PTY device ready: ${RAPI_PTY_PATH}"

/usr/local/bin/openevse --rapi-serial "${RAPI_PTY_PATH}" "$@" &
NATIVE_PID=$!

wait "$NATIVE_PID"
