#!/usr/bin/env bash
#
# Test and diagnostic harness for AI agents (and humans) working in a sandbox.
#
# The sandbox gives every Bash invocation its own network namespace, so a
# server started in one command is gone by the next. Every subcommand here
# therefore launches what it needs, uses it, and tears it down inside a single
# invocation — pass the thing you want to run with `-- <cmd>`.
#
#   scripts/openevse_test.sh unit                     # host doctest suites
#   scripts/openevse_test.sh divert                   # divert_sim scenarios
#   scripts/openevse_test.sh gui                      # web UI vitest
#   scripts/openevse_test.sh all                       # the three above
#   scripts/openevse_test.sh native -n 2 -- curl -s localhost:8000/config
#   scripts/openevse_test.sh integration -v            # emulator-backed pytest
#   scripts/openevse_test.sh emulator -- pytest tests/integration/
#
# `native` runs sandboxed: each instance gets a loopback PTY with nothing on
# the far end, so RAPI never answers and /status stays state=0 (STARTING).
# That is fine for config/claims/override diagnostics. Anything that depends on
# real EVSE state needs `emulator` or `integration`, which drive the Docker
# emulator and so must run outside the sandbox (see docs/ai/sandbox.md).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

# Port bases match tests/integration/conftest.py so both harnesses agree.
native_base=8000
emu_web_base=8080
emu_rapi_base=8023

native_binary="${NATIVE_BINARY_PATH:-.pio/build/native_openevse/program}"
sim_binary=".pio/build/native_simulator/program"
emulator_image="${OPENEVSE_EMULATOR_IMAGE:-ghcr.io/jeremypoulter/openevse_emulator:latest}"

# /tmp is read-only inside the sandbox; TMPDIR is not.
run_dir="$(mktemp -d "${TMPDIR:-/tmp}/openevse-harness.XXXXXX")"

pids=()
containers=()

log() { printf '==> %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

cleanup() {
  local status=$?
  set +e
  for pid in "${pids[@]:-}"; do
    [ -n "$pid" ] || continue
    kill "$pid" 2>/dev/null
  done
  # Give the firmware a moment to close its listeners, then insist.
  sleep 0.5
  for pid in "${pids[@]:-}"; do
    [ -n "$pid" ] || continue
    kill -9 "$pid" 2>/dev/null
  done
  for name in "${containers[@]:-}"; do
    [ -n "$name" ] || continue
    docker rm -f "$name" >/dev/null 2>&1
  done
  rm -rf "$run_dir"
  return $status
}
trap cleanup EXIT

require_native_binary() {
  if [ ! -x "$native_binary" ]; then
    log "building $native_binary (first build pulls the toolchain — do not cancel)"
    pio run -e native_openevse
  fi
  [ -x "$native_binary" ] || die "native binary not found at $native_binary"
  native_binary="$(realpath "$native_binary")"
}

wait_for_http() {
  local url=$1 timeout=${2:-30} deadline
  deadline=$((SECONDS + timeout))
  while [ $SECONDS -lt $deadline ]; do
    if curl -fsS -o /dev/null --max-time 2 "$url" 2>/dev/null; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

# Start one native firmware instance: $1 = index, $2 = RAPI PTY path.
start_native() {
  local idx=$1 pty=$2 port=$((native_base + $1)) workdir="$run_dir/native$1"
  mkdir -p "$workdir"
  (
    cd "$workdir"
    exec "$native_binary" --rapi-serial "$pty" --set-config "www_http_port=$port" \
      > "$run_dir/native$idx.log" 2>&1
  ) &
  pids+=("$!")
  if ! wait_for_http "http://localhost:$port/config" 30; then
    log "instance $idx did not come up on port $port; last log lines:"
    tail -20 "$run_dir/native$idx.log" >&2 || true
    die "native firmware failed to start"
  fi
  log "instance $idx ready on http://localhost:$port (log: $run_dir/native$idx.log)"
}

# Export EVSE_URL_<i> / EVSE_URLS so the wrapped command can find the instances.
export_instance_env() {
  local count=$1 urls=() i
  for ((i = 0; i < count; i++)); do
    export "EVSE_URL_$i=http://localhost:$((native_base + i))"
    urls+=("http://localhost:$((native_base + i))")
  done
  export EVSE_COUNT="$count"
  export EVSE_URL="${urls[0]}"
  export EVSE_URLS="${urls[*]}"
}

# Print a short status dump for each instance — the default when no command is
# given, and the quickest way to see that a change did not break the API.
status_dump() {
  local count=$1 i url
  for ((i = 0; i < count; i++)); do
    url="http://localhost:$((native_base + i))"
    printf '\n--- instance %d (%s) ---\n' "$i" "$url"
    for path in /status /config /claims; do
      printf '%s: ' "$path"
      curl -fsS --max-time 5 "$url$path" 2>/dev/null | head -c 400 || printf '(request failed)'
      printf '\n'
    done
  done
}

cmd_unit() {
  log "host unit tests (pio test -e native_test)"
  pio test -e native_test "$@"
}

cmd_divert() {
  # run_simulations.py prefers ./divert_sim, then .pio/build/native_simulator.
  if [ ! -x "divert_sim/divert_sim" ] && [ ! -x "$sim_binary" ]; then
    log "building $sim_binary"
    pio run -e native_simulator
  fi
  log "divert_sim scenario tests"
  local pytest_bin=pytest
  [ -x "divert_sim/.venv/bin/pytest" ] && pytest_bin="$repo_root/divert_sim/.venv/bin/pytest"
  (cd divert_sim && "$pytest_bin" "${@:--v}")
}

cmd_gui() {
  [ -d gui-nightshift/src ] || die "gui-nightshift not checked out — run: git submodule update --init --recursive"
  if [ ! -d gui-nightshift/node_modules ]; then
    # `npm ci` rather than `npm install`: it installs exactly the lockfile and
    # never rewrites it, which matters because gui-nightshift is a submodule and
    # a stray package-lock.json change would dirty its pointer.
    log "installing gui-nightshift dependencies"
    if [ -f gui-nightshift/package-lock.json ]; then
      (cd gui-nightshift && npm ci)
    else
      (cd gui-nightshift && npm install)
    fi
  fi
  log "web UI unit tests"
  (cd gui-nightshift && npm test "$@")
}

cmd_all() {
  cmd_unit
  cmd_divert
  cmd_gui
}

# native [-n COUNT] [-- cmd ...]
cmd_native() {
  local count=1
  while [ $# -gt 0 ]; do
    case $1 in
      -n|--instances) count=$2; shift 2 ;;
      --) shift; break ;;
      *) die "unexpected argument to 'native': $1" ;;
    esac
  done

  require_native_binary
  local i pty
  for ((i = 0; i < count; i++)); do
    pty="$run_dir/rapi_pty_$i"
    # Loopback PTY pair: the firmware opens one end, nothing answers on the
    # other. Keeps the serial port open so the firmware boots and serves HTTP.
    socat "PTY,link=$pty,rawer,wait-slave" "PTY,rawer" >/dev/null 2>&1 &
    pids+=("$!")
    export "EVSE_PTY_$i=$pty"
    local waited=0
    while [ ! -e "$pty" ] && [ $waited -lt 50 ]; do sleep 0.1; waited=$((waited + 1)); done
    [ -e "$pty" ] || die "socat did not create $pty"
    start_native "$i" "$pty"
  done

  export_instance_env "$count"
  if [ $# -gt 0 ]; then
    "$@"
  else
    status_dump "$count"
  fi
}

# emulator [-n COUNT] [-- cmd ...] — needs Docker, so runs outside the sandbox.
cmd_emulator() {
  local count=1
  while [ $# -gt 0 ]; do
    case $1 in
      -n|--instances) count=$2; shift 2 ;;
      --) shift; break ;;
      *) die "unexpected argument to 'emulator': $1" ;;
    esac
  done

  docker info >/dev/null 2>&1 || die "docker unreachable. The sandbox blocks the docker socket and host-published ports — run this subcommand outside the sandbox (see docs/ai/sandbox.md)."
  require_native_binary
  docker image inspect "$emulator_image" >/dev/null 2>&1 || docker pull "$emulator_image"

  local i name web_port rapi_port pty
  for ((i = 0; i < count; i++)); do
    name="openevse_emulator_$i"
    web_port=$((emu_web_base + i))
    rapi_port=$((emu_rapi_base + i))
    pty="$run_dir/rapi_pty_$i"

    docker rm -f "$name" >/dev/null 2>&1 || true
    docker run -d --name "$name" \
      -p "$web_port:8080" -p "$rapi_port:8023" \
      -e WEB_PORT=8080 -e SERIAL_MODE=tcp -e SERIAL_TCP_PORT=8023 \
      "$emulator_image" >/dev/null
    containers+=("$name")
    wait_for_http "http://localhost:$web_port/api/status" 30 \
      || die "emulator $i did not become ready on port $web_port"
    log "emulator $i ready on http://localhost:$web_port"

    socat "PTY,link=$pty,rawer,wait-slave" "TCP:localhost:$rapi_port" >/dev/null 2>&1 &
    pids+=("$!")
    export "EVSE_PTY_$i=$pty" "EMULATOR_URL_$i=http://localhost:$web_port"
    local waited=0
    while [ ! -e "$pty" ] && [ $waited -lt 50 ]; do sleep 0.1; waited=$((waited + 1)); done
    [ -e "$pty" ] || die "socat did not bridge $pty to port $rapi_port"
    start_native "$i" "$pty"
  done

  export_instance_env "$count"
  if [ $# -gt 0 ]; then
    "$@"
  else
    status_dump "$count"
  fi
}

# integration — the checked-in pytest suite, which builds its own emulator and
# firmware instances via tests/integration/conftest.py. Needs Docker and a
# writable /tmp for its PTY, so it runs outside the sandbox.
cmd_integration() {
  docker info >/dev/null 2>&1 || die "docker unreachable. The integration suite needs the docker socket and a writable /tmp — run it outside the sandbox (see docs/ai/sandbox.md)."
  require_native_binary
  local pytest_bin=pytest
  [ -x ".venv/bin/pytest" ] && pytest_bin="$repo_root/.venv/bin/pytest"
  log "integration tests (emulator + native firmware)"
  NATIVE_BINARY_PATH="$native_binary" "$pytest_bin" tests/integration/ "${@:--v}"
}

usage() {
  # Print the header comment block (everything after the shebang up to the
  # first line of code), with the comment markers stripped.
  awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "${BASH_SOURCE[0]}"
}

command=${1:-}
[ $# -gt 0 ] && shift
case $command in
  unit)        cmd_unit "$@" ;;
  divert)      cmd_divert "$@" ;;
  gui)         cmd_gui "$@" ;;
  all)         cmd_all "$@" ;;
  native)      cmd_native "$@" ;;
  emulator)    cmd_emulator "$@" ;;
  integration) cmd_integration "$@" ;;
  ""|-h|--help|help) usage ;;
  *) die "unknown command '$command' (try --help)" ;;
esac
