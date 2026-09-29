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
#   scripts/openevse_test.sh launch -i 1               # one long-lived pair
#   scripts/openevse_test.sh launch -i 1 --pr 1027     # ... running a PR's build
#
# `native` runs sandboxed: each instance gets a loopback PTY with nothing on
# the far end, so RAPI never answers and /status stays state=0 (STARTING).
# That is fine for config/claims/override diagnostics. Anything that depends on
# real EVSE state needs `emulator` or `integration`, which drive the Docker
# emulator and so must run outside the sandbox (see docs/ai/sandbox.md).
#
# `launch` is the interactive counterpart: it brings up ONE emulator + firmware
# pair for a given instance ID and stays in the foreground until Ctrl-C, so a
# REST client (test/loadsharing.http) or another terminal can drive it. Every
# derived setting can be overridden:
#
#   launch [-i ID] [--http-port P] [--web-port P] [--rapi-port P]
#          [--rapi-serial PATH] [--chip-id HEX] [--hostname NAME]
#          [--workdir DIR] [--set-config NAME=VALUE]...
#          [--emulator docker|local|none] [--local] [--emulator-dir DIR]
#          [--emulator-image IMAGE] [--no-emulator]
#          [--firmware docker|local] [--firmware-image IMAGE]
#          [--firmware-tag TAG] [--pr N]
#          [--[no-]firmware-console] [--[no-]emulator-console]
#          [--console | --no-console] [-- cmd ...]
#
# Defaults for instance ID i: firmware HTTP 8000+i, firmware HTTPS 8443+i,
# emulator web 8080+i, emulator RAPI 8023+i, chip ID 0x1234<5678+i>90ABCDEF,
# hostname openevse-ev<i>, working directory test/<i> (so EEPROM/LittleFS state
# persists per instance). The emulator runs from the Docker image by default
# (bridged to the firmware's PTY with socat); `--local` runs it from a source
# checkout instead, which needs no Docker and no socat.
#
# The FIRMWARE is the local native build by default. `--firmware docker` runs it
# from the image built by .github/workflows/native_docker.yaml instead, which is
# how you try someone else's build without building it yourself:
#
#   --pr N               ghcr.io/openevse/openevse-wifi-native:pr-N
#   --firmware-tag TAG   the same repo at an arbitrary tag (latest, a version)
#   --firmware-image REF any image at all, for a locally built one
#
# The image's entrypoint runs socat itself, so a containerised firmware takes
# RAPI over TCP rather than a PTY and reaches the emulator back through
# host.docker.internal. That makes --workdir and --rapi-serial meaningless
# there, and both are rejected rather than ignored.
#
# The firmware console is mirrored to this terminal as `[fw<i>] ...`; the
# emulator console (`[emu<i>] ...`) is off by default because it logs every RAPI
# poll. Both always go to their log file under the run directory either way.
set -euo pipefail

# Resolve our own path before the cd below: a relative BASH_SOURCE
# (./openevse_test.sh from scripts/) stops resolving once the cwd changes, and
# `usage` reads this file to print its own header.
script_path="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

# Port bases match tests/integration/conftest.py so both harnesses agree.
native_base=8000
native_tls_base=8443
emu_web_base=8080
emu_rapi_base=8023

native_binary="${NATIVE_BINARY_PATH:-.pio/build/native_openevse/program}"
sim_binary=".pio/build/native_simulator/program"
emulator_image="${OPENEVSE_EMULATOR_IMAGE:-ghcr.io/jeremypoulter/openevse_emulator:latest}"

# Firmware-from-Docker: the image built by .github/workflows/native_docker.yaml.
# Its entrypoint runs socat itself (TCP -> PTY) and then the firmware, so the
# host does not bridge anything for a containerised instance.
native_image_repo="${OPENEVSE_NATIVE_IMAGE_REPO:-ghcr.io/openevse/openevse-wifi-native}"
native_image="${OPENEVSE_NATIVE_IMAGE:-$native_image_repo:latest}"
emulator_dir="${OPENEVSE_EMULATOR_DIR:-$repo_root/../OpenEVSE_Emulator}"

# /tmp is read-only inside the sandbox; TMPDIR is not.
run_dir="$(mktemp -d "${TMPDIR:-/tmp}/openevse-harness.XXXXXX")"

pids=()
containers=()
networks=()

# Set when the firmware and the emulator are both containers: they then talk
# over a private network by container name, so the emulator's RAPI port stays
# published on loopback only (or not at all) rather than being opened up to
# reach the firmware container.
docker_network=""

# Parallel arrays describing the instances this invocation started, so the
# env export and the status dump work off real instance IDs rather than
# assuming a 0..count-1 range (`launch -i 1` starts a single instance 1).
inst_index=()
inst_name=()
inst_url=()

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
  # After the containers, or the network is still in use.
  for name in "${networks[@]:-}"; do
    [ -n "$name" ] || continue
    docker network rm "$name" >/dev/null 2>&1
  done
  rm -rf "$run_dir"
  return $status
}
trap cleanup EXIT
# `launch` sits in the foreground waiting for Ctrl-C, so make sure a signal
# still routes through cleanup rather than leaving containers behind.
trap 'exit 130' INT
trap 'exit 143' TERM

is_number() {
  case ${1:-} in
    ''|*[!0-9]*) return 1 ;;
    *) return 0 ;;
  esac
}

# Read the value of a flag, failing usefully when it was left off the end.
# Without this a trailing `--http-port` silently consumes the `--` separator.
arg_value() {
  local flag=$1 value=${2:-}
  [ -n "$value" ] || die "$flag requires a value"
  printf '%s' "$value"
}

instance_name() { printf 'openevse-ev%d' "$1"; }

# ESPAL treats the chip id as a MAC: getLongId() reverses the bytes and shifts
# right by 16, and getShortId() keeps the last ESPAL_SHORT_ID_LENGTH (4) hex
# digits of that. Those digits come from bytes 2-3 of the chip id, so varying
# the low byte would leave every instance sharing a short id — and with it the
# default hostname, the softAP SSID and the MQTT announce topic. Vary the
# 0x5678 field instead, leaving instance 0 on the firmware's own default of
# 0x1234567890ABCDEF.
instance_chip_id() { printf '0x1234%04X90ABCDEF' "$((0x5678 + $1))"; }

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

# Wait (up to 5s) for a path — a PTY link, usually — to appear.
wait_for_path() {
  local path=$1 waited=0
  while [ ! -e "$path" ] && [ $waited -lt 50 ]; do sleep 0.1; waited=$((waited + 1)); done
  [ -e "$path" ]
}

# The HTTP server comes up before the first RAPI reply arrives, so /status
# reports state=0 (STARTING) for a moment after wait_for_http succeeds. Wait it
# out, or a wrapped command sees a firmware that is up but has not yet talked
# to its EVSE. Only meaningful when there is an emulator on the far end.
wait_for_evse_state() {
  local url=$1 timeout=${2:-30} deadline body
  deadline=$((SECONDS + timeout))
  while [ $SECONDS -lt $deadline ]; do
    body="$(curl -fsS --max-time 2 "$url/status" 2>/dev/null)" || body=
    if [ -n "$body" ] && ! printf '%s' "$body" | grep -q '"state":[[:space:]]*0[,}]'; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

# Console pass-through, set by `launch`: everything still goes to its log file,
# these only decide whether it is also mirrored to this terminal.
firmware_console=0
emulator_console=0

# Mirror a console to stderr, tagged with $2:
#   follow_console NAME TAG -- producer command ...
# The producer is the pid we track and kill; the `sed` that adds the tag reads
# from a FIFO, so it ends by itself when the producer closes it. Tagging with a
# plain `producer | sed` pipeline instead would leave the producer untracked
# ($! is the last pipeline member), and a `tail --pid` using inotify can sit on
# a dead writer indefinitely.
#
# socat notice/info/debug lines are dropped on the way through. Images built
# before the entrypoint stopped passing `-d -d` log a line per write — two per
# RAPI poll, forever — which buries the firmware's own console, and
# `--firmware-tag latest` still pulls one of those. Fatal, error and warning
# lines are kept, so a refused connection to the emulator still shows up. The
# log file behind the mirror keeps everything either way: for a container it is
# `docker logs`, which is untouched.
follow_console() {
  local name=$1 tag=$2 fifo="$run_dir/$1.console"
  shift 3
  mkfifo "$fifo"
  sed -u -e '/ socat\[[0-9]\{1,\}\] [NID] /d' -e "s/^/$tag /" < "$fifo" >&2 &
  "$@" > "$fifo" 2>&1 &
  pids+=("$!")
}

# Start one native firmware instance:
#   $1 = index, $2 = hostname, $3 = chip ID, $4 = RAPI PTY path,
#   $5 = HTTP port, $6 = working directory, $7.. = extra arguments.
# The chip ID reaches the firmware as OPENEVSE_CHIP_ID, which ESPAL's
# EpoxyDuino HAL reads — that is what gives each peer a distinct identity.
# Records the instance in inst_* for export_instance_env and status_dump.
start_native() {
  local idx=$1 name=$2 chip_id=$3 pty=$4 port=$5 workdir=$6
  local logfile="$run_dir/native$1.log" native_pid tls_port=$((native_tls_base + $1))
  shift 6
  mkdir -p "$workdir"
  (
    cd "$workdir"
    export OPENEVSE_CHIP_ID="$chip_id"
    exec "$native_binary" --rapi-serial "$pty" \
      --set-config "hostname=$name" \
      --set-config "www_http_port=$port" \
      --set-config "www_https_port=$tls_port" \
      "$@" > "$logfile" 2>&1
  ) &
  native_pid=$!
  pids+=("$native_pid")
  if ! wait_for_http "http://localhost:$port/config" 30; then
    log "instance $idx did not come up on port $port; last log lines:"
    tail -20 "$logfile" >&2 || true
    die "native firmware failed to start"
  fi
  log "instance $idx ($name, chip id $chip_id) ready on http://localhost:$port (log: $logfile)"
  if [ "$firmware_console" = 1 ]; then
    follow_console "fw$idx" "[fw$idx]" -- tail --pid="$native_pid" -n +1 -F "$logfile"
  fi
  inst_index+=("$idx")
  inst_name+=("$name")
  inst_url+=("http://localhost:$port")
}

# Start one firmware instance from the Docker image instead of a local build:
#   $1 = index, $2 = hostname, $3 = chip ID, $4 = RAPI TCP port on the host,
#   $5 = HTTP port, $6.. = extra arguments for the binary.
# The image's entrypoint runs socat (TCP -> PTY) and then the firmware, so the
# RAPI link is a TCP port here rather than a PTY the host has to bridge. The
# emulator is reached back out through host-gateway, which works whether it is
# another container publishing a port or a source checkout listening on one.
start_native_docker() {
  local idx=$1 name=$2 chip_id=$3 rapi_host=$4 rapi_port=$5 port=$6
  local container="openevse_native_$1" tls_port=$((native_tls_base + $1))
  shift 6
  docker rm -f "$container" >/dev/null 2>&1 || true
  docker run -d --name "$container" \
    ${docker_network:+--network "$docker_network"} \
    --add-host "host.docker.internal:host-gateway" \
    -p "127.0.0.1:$port:$port" -p "127.0.0.1:$tls_port:$tls_port" \
    -e "OPENEVSE_CHIP_ID=$chip_id" \
    -e "EMULATOR_HOST=$rapi_host" \
    -e "EMULATOR_PORT=$rapi_port" \
    "$native_image" \
    --set-config "hostname=$name" \
    --set-config "www_http_port=$port" \
    --set-config "www_https_port=$tls_port" \
    "$@" >/dev/null
  containers+=("$container")
  if ! wait_for_http "http://localhost:$port/config" 60; then
    log "instance $idx did not come up on port $port; last log lines:"
    docker logs --tail 20 "$container" >&2 2>&1 || true
    die "containerised firmware failed to start"
  fi
  log "instance $idx ($name, chip id $chip_id) ready on http://localhost:$port (container $container, image $native_image)"
  if [ "$firmware_console" = 1 ]; then
    # No host log file — the console lives in the container.
    follow_console "fw$idx" "[fw$idx]" -- docker logs -f --tail=all "$container"
  fi
  inst_index+=("$idx")
  inst_name+=("$name")
  inst_url+=("http://localhost:$port")
}

# A TCP listener that accepts the containerised firmware's RAPI connection and
# never answers — the TCP equivalent of start_loopback_pty, for --no-emulator.
# Without something listening, the socat inside the image exits and takes the
# entrypoint with it.
start_null_rapi_listener() {
  local rapi_port=$1
  # Bound to all interfaces, not loopback: the client is inside a container and
  # arrives via the bridge. It is a sink that never replies, so there is nothing
  # to reach.
  socat "TCP-LISTEN:$rapi_port,reuseaddr,fork" "PTY,rawer" >/dev/null 2>&1 &
  pids+=("$!")
}

# Loopback PTY pair at $1: the firmware opens one end, nothing answers on the
# other. Keeps the serial port open so the firmware boots and serves HTTP.
start_loopback_pty() {
  local pty=$1
  socat "PTY,link=$pty,rawer,wait-slave" "PTY,rawer" >/dev/null 2>&1 &
  pids+=("$!")
  wait_for_path "$pty" || die "socat did not create $pty"
}

# Bridge a PTY at $1 to the emulator's TCP RAPI port $2. Needed for the Docker
# emulator: a PTY inside the container is invisible to the host, so the
# container serves RAPI over TCP and socat gives the firmware a serial port.
bridge_pty_to_tcp() {
  local pty=$1 rapi_port=$2
  socat "PTY,link=$pty,rawer,wait-slave" "TCP:localhost:$rapi_port" >/dev/null 2>&1 &
  pids+=("$!")
  wait_for_path "$pty" || die "socat did not bridge $pty to port $rapi_port"
}

# Pull the firmware image if it is not already local. A missing pr-N tag is the
# common case and worth explaining: the image is built for every PR but only
# published for branches in this repo, because a fork's GITHUB_TOKEN cannot
# write packages.
require_native_image() {
  local pr=${1:-}
  docker image inspect "$native_image" >/dev/null 2>&1 && return 0
  log "pulling $native_image"
  if docker pull "$native_image" >/dev/null 2>&1; then
    return 0
  fi
  if [ -n "$pr" ]; then
    die "no image published for PR #$pr ($native_image).
  Either the build has not finished yet, or the PR is from a fork — fork builds
  cannot push to GHCR. Check the 'Native Docker Image' run on the PR, or use
  --firmware-image IMAGE with an image you have, or drop --pr to build locally."
  fi
  die "could not pull $native_image — check the tag exists, or pass --firmware-image IMAGE"
}

require_docker() {
  docker info >/dev/null 2>&1 || die "docker unreachable. The sandbox blocks the docker socket and host-published ports — run this subcommand outside the sandbox (see docs/ai/sandbox.md)."
  docker image inspect "$emulator_image" >/dev/null 2>&1 || docker pull "$emulator_image"
}

# Emulator from the Docker image: $1 = index, $2 = container name,
# $3 = published web port, $4 = published RAPI TCP port.
start_emulator_docker() {
  local idx=$1 name=$2 web_port=$3 rapi_port=$4
  docker rm -f "$name" >/dev/null 2>&1 || true
  # Publish on loopback only — these are test instances with no auth in front.
  docker run -d --name "$name" \
    ${docker_network:+--network "$docker_network"} \
    -p "127.0.0.1:$web_port:8080" -p "127.0.0.1:$rapi_port:8023" \
    -e WEB_PORT=8080 -e SERIAL_MODE=tcp -e SERIAL_TCP_PORT=8023 \
    -e PYTHONUNBUFFERED=1 \
    "$emulator_image" >/dev/null
  containers+=("$name")
  wait_for_http "http://localhost:$web_port/api/status" 30 \
    || die "emulator $idx did not become ready on port $web_port"
  log "emulator $idx ready on http://localhost:$web_port (container $name)"
  if [ "$emulator_console" = 1 ]; then
    # No log file to follow here — the console lives in the container.
    follow_console "emu$idx" "[emu$idx]" -- docker logs -f --tail=all "$name"
  fi
}

# Emulator from a source checkout: $1 = index, $2 = web port, $3 = PTY path,
# $4 = RAPI TCP port (empty for PTY mode).
# In PTY mode the emulator creates the PTY itself, so no socat bridge is needed.
# A containerised firmware cannot see a host PTY, so it asks for TCP mode
# instead and the socat inside the image does the bridging.
start_emulator_local() {
  local idx=$1 web_port=$2 pty=$3 rapi_port=${4:-}
  local python_bin=python3 logfile="$run_dir/emulator$1.log" emu_pid
  local serial_args
  [ -f "$emulator_dir/src/main.py" ] \
    || die "no emulator checkout at $emulator_dir — clone https://github.com/jeremypoulter/OpenEVSE_Emulator, pass --emulator-dir DIR, or drop --local to use the docker image"
  [ -x "$emulator_dir/.venv/bin/python" ] && python_bin="$emulator_dir/.venv/bin/python"
  if [ -n "$rapi_port" ]; then
    serial_args="--serial-mode tcp --serial-tcp-port $rapi_port"
  else
    serial_args="--serial-mode pty --serial-pty-path $pty"
  fi
  (
    cd "$emulator_dir"
    # -u so the log (and the mirrored console) is not held in python's buffer.
    # shellcheck disable=SC2086 # serial_args is built above, deliberately split
    exec "$python_bin" -u src/main.py $serial_args \
      --web-host 127.0.0.1 --web-port "$web_port" \
      > "$logfile" 2>&1
  ) &
  emu_pid=$!
  pids+=("$emu_pid")
  if ! wait_for_http "http://localhost:$web_port/api/status" 30; then
    log "emulator $idx did not become ready on port $web_port; last log lines:"
    tail -20 "$logfile" >&2 || true
    die "local emulator failed to start — missing deps? pip install -r $emulator_dir/requirements.txt (or drop --local to use the docker image)"
  fi
  if [ -z "$rapi_port" ]; then
    wait_for_path "$pty" || die "emulator did not create $pty"
  fi
  log "emulator $idx ready on http://localhost:$web_port (source: $emulator_dir, log: $logfile)"
  if [ "$emulator_console" = 1 ]; then
    follow_console "emu$idx" "[emu$idx]" -- tail --pid="$emu_pid" -n +1 -F "$logfile"
  fi
}

# Export EVSE_URL_<i> / EVSE_URLS so the wrapped command can find the instances.
# Driven off what start_native actually recorded, so it is right for `launch`,
# whose single instance is not necessarily numbered 0.
export_instance_env() {
  local i
  for i in "${!inst_index[@]}"; do
    export "EVSE_URL_${inst_index[$i]}=${inst_url[$i]}"
    export "EVSE_NAME_${inst_index[$i]}=${inst_name[$i]}"
  done
  export EVSE_COUNT="${#inst_index[@]}"
  export EVSE_URL="${inst_url[0]}"
  export EVSE_URLS="${inst_url[*]}"
  export EVSE_NAMES="${inst_name[*]}"
}

# Print a short status dump for each instance — the default when no command is
# given, and the quickest way to see that a change did not break the API.
status_dump() {
  local i path body="$run_dir/.status_dump"
  for i in "${!inst_index[@]}"; do
    printf '\n--- instance %s (%s, %s) ---\n' \
      "${inst_index[$i]}" "${inst_name[$i]}" "${inst_url[$i]}"
    for path in /status /config /claims; do
      printf '%s: ' "$path"
      # Fetch to a file rather than piping into `head`: under `set -o pipefail`
      # a body big enough to outlive head's 400 bytes kills curl with EPIPE, and
      # the pipeline then reports failure after printing perfectly good output.
      if curl -fsS --max-time 5 -o "$body" "${inst_url[$i]}$path" 2>/dev/null; then
        head -c 400 "$body"
      else
        printf '(request failed)'
      fi
      printf '\n'
    done
  done
  rm -f "$body"
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
      -n|--instances) count=$(arg_value "$1" "${2:-}"); shift 2 ;;
      --) shift; break ;;
      *) die "unexpected argument to 'native': $1" ;;
    esac
  done
  { is_number "$count" && [ "$count" -ge 1 ]; } \
    || die "-n takes a positive number (got '$count')"

  require_native_binary
  local i pty
  for ((i = 0; i < count; i++)); do
    pty="$run_dir/rapi_pty_$i"
    start_loopback_pty "$pty"
    export "EVSE_PTY_$i=$pty"
    start_native "$i" "$(instance_name "$i")" "$(instance_chip_id "$i")" \
      "$pty" "$((native_base + i))" "$run_dir/native$i"
  done

  export_instance_env
  if [ $# -gt 0 ]; then
    "$@"
  else
    status_dump
  fi
}

# emulator [-n COUNT] [-- cmd ...] — needs Docker, so runs outside the sandbox.
cmd_emulator() {
  local count=1
  while [ $# -gt 0 ]; do
    case $1 in
      -n|--instances) count=$(arg_value "$1" "${2:-}"); shift 2 ;;
      --) shift; break ;;
      *) die "unexpected argument to 'emulator': $1" ;;
    esac
  done
  { is_number "$count" && [ "$count" -ge 1 ]; } \
    || die "-n takes a positive number (got '$count')"

  require_docker
  require_native_binary

  local i name http_port web_port rapi_port pty
  for ((i = 0; i < count; i++)); do
    name="openevse_emulator_$i"
    http_port=$((native_base + i))
    web_port=$((emu_web_base + i))
    rapi_port=$((emu_rapi_base + i))
    pty="$run_dir/rapi_pty_$i"

    start_emulator_docker "$i" "$name" "$web_port" "$rapi_port"
    bridge_pty_to_tcp "$pty" "$rapi_port"
    export "EVSE_PTY_$i=$pty" "EMULATOR_URL_$i=http://localhost:$web_port"
    start_native "$i" "$(instance_name "$i")" "$(instance_chip_id "$i")" \
      "$pty" "$http_port" "$run_dir/native$i"
    wait_for_evse_state "http://localhost:$http_port" 30 \
      || die "instance $i never left state=0 (STARTING) — RAPI is not getting through to the emulator on port $rapi_port"
  done

  export_instance_env
  if [ $# -gt 0 ]; then
    "$@"
  else
    status_dump
  fi
}

# launch [-i ID] [overrides ...] [-- cmd ...] — one emulator + firmware pair,
# left running in the foreground so a REST client can drive it. Everything is
# derived from the instance ID; every derived value has an override. Needs
# Docker unless --local or --no-emulator is given.
cmd_launch() {
  local id=0 http_port= web_port= rapi_port= pty= chip_id= hostname= workdir=
  local emulator_mode=docker firmware_mode=local pr=
  local extra_config=()
  # Firmware console on, emulator console off — the firmware is what you are
  # usually debugging, the emulator is chatty about every RAPI poll.
  firmware_console=1
  emulator_console=0
  while [ $# -gt 0 ]; do
    case $1 in
      -i|--instance)          id=$(arg_value "$1" "${2:-}"); shift 2 ;;
      --set-config)           extra_config+=(--set-config "$(arg_value "$1" "${2:-}")"); shift 2 ;;
      --firmware-console)     firmware_console=1; shift ;;
      --no-firmware-console)  firmware_console=0; shift ;;
      --emulator-console)     emulator_console=1; shift ;;
      --no-emulator-console)  emulator_console=0; shift ;;
      --console)              firmware_console=1; emulator_console=1; shift ;;
      --no-console)           firmware_console=0; emulator_console=0; shift ;;
      --http-port)            http_port=$(arg_value "$1" "${2:-}"); shift 2 ;;
      --web-port)             web_port=$(arg_value "$1" "${2:-}"); shift 2 ;;
      --rapi-port)            rapi_port=$(arg_value "$1" "${2:-}"); shift 2 ;;
      --rapi-serial|--pty)    pty=$(arg_value "$1" "${2:-}"); shift 2 ;;
      --chip-id)              chip_id=$(arg_value "$1" "${2:-}"); shift 2 ;;
      --hostname)             hostname=$(arg_value "$1" "${2:-}"); shift 2 ;;
      -C|--workdir)           workdir=$(arg_value "$1" "${2:-}"); shift 2 ;;
      --emulator)             emulator_mode=$(arg_value "$1" "${2:-}"); shift 2 ;;
      --local|--local-source) emulator_mode=local; shift ;;
      --docker)               emulator_mode=docker; shift ;;
      --firmware)             firmware_mode=$(arg_value "$1" "${2:-}"); shift 2 ;;
      --firmware-image)       native_image=$(arg_value "$1" "${2:-}"); firmware_mode=docker; shift 2 ;;
      --firmware-tag)         native_image="$native_image_repo:$(arg_value "$1" "${2:-}")"; firmware_mode=docker; shift 2 ;;
      --pr)                   pr=$(arg_value "$1" "${2:-}"); firmware_mode=docker; shift 2 ;;
      --emulator-dir)         emulator_dir=$(arg_value "$1" "${2:-}"); emulator_mode=local; shift 2 ;;
      --emulator-image)       emulator_image=$(arg_value "$1" "${2:-}"); emulator_mode=docker; shift 2 ;;
      --no-emulator)          emulator_mode=none; shift ;;
      --) shift; break ;;
      *) die "unexpected argument to 'launch': $1" ;;
    esac
  done

  case $emulator_mode in
    docker|local|none) ;;
    *) die "--emulator takes docker, local or none (got '$emulator_mode')" ;;
  esac
  case $firmware_mode in
    docker|local) ;;
    *) die "--firmware takes docker or local (got '$firmware_mode')" ;;
  esac
  if [ -n "$pr" ]; then
    is_number "$pr" || die "--pr takes a pull request number (got '$pr')"
    native_image="$native_image_repo:pr-$pr"
  fi
  if [ "$firmware_mode" = docker ]; then
    # The container brings its own filesystem and makes its own PTY, so these
    # would be silently ignored rather than doing what they say.
    [ -z "$workdir" ] || die "--workdir has no meaning with a containerised firmware — its state lives in the container"
    [ -z "$pty" ] || die "--rapi-serial has no meaning with a containerised firmware — it bridges RAPI over tcp/$((emu_rapi_base + id)) instead"
  fi
  is_number "$id" || die "instance ID must be a non-negative integer (got '$id')"
  local port
  for port in "$http_port" "$web_port" "$rapi_port"; do
    [ -z "$port" ] || is_number "$port" || die "port arguments take a number (got '$port')"
  done

  : "${http_port:=$((native_base + id))}"
  : "${web_port:=$((emu_web_base + id))}"
  : "${rapi_port:=$((emu_rapi_base + id))}"
  : "${pty:=$run_dir/rapi_pty_$id}"
  : "${chip_id:=$(instance_chip_id "$id")}"
  : "${hostname:=$(instance_name "$id")}"
  : "${workdir:=$repo_root/test/$id}"
  # The emulator runs with its own cwd, so it needs an absolute PTY path.
  [[ $pty == /* ]] || pty="$repo_root/$pty"

  local consoles=()
  [ "$firmware_console" = 1 ] && consoles+=("[fw$id] firmware")
  [ "$emulator_console" = 1 ] && consoles+=("[emu$id] emulator")
  local console_summary="off (logs only)"
  if [ ${#consoles[@]} -gt 0 ]; then
    console_summary="$(printf '%s, ' "${consoles[@]}")"
    console_summary="${console_summary%, }"
  fi

  # Printed before anything starts, so it is not buried under the console
  # output that follows.
  cat >&2 <<EOF

instance     $id
firmware     http://localhost:$http_port  $(case $firmware_mode in
                local)  printf '(local build %s, workdir %s)' "$native_binary" "$workdir" ;;
                docker) printf '(docker image %s)' "$native_image" ;;
              esac)
emulator     $(case $emulator_mode in
                docker) printf 'http://localhost:%s  (docker image %s)' "$web_port" "$emulator_image" ;;
                local)  printf 'http://localhost:%s  (source %s)' "$web_port" "$emulator_dir" ;;
                none)   printf 'none' ;;
              esac)
rapi link    $(if [ "$firmware_mode" = docker ] && [ "$emulator_mode" = docker ]; then
                printf 'openevse_emulator_%s:8023 over a private docker network' "$id"
              elif [ "$firmware_mode" = docker ]; then
                printf 'host.docker.internal:%s (bridged inside the firmware container)' "$rapi_port"
              elif [ "$emulator_mode" = docker ]; then
                printf '%s -> tcp/%s' "$pty" "$rapi_port"
              else
                printf '%s' "$pty"
              fi)
chip ID      $chip_id
hostname     $hostname
console      $console_summary
logs         $run_dir

EOF

  { [ "$emulator_mode" = docker ] || [ "$firmware_mode" = docker ]; } && require_docker
  if [ "$firmware_mode" = docker ]; then
    require_native_image "$pr"
  else
    require_native_binary
  fi

  # A containerised firmware reaches RAPI over TCP, so the emulator has to offer
  # a TCP port rather than a PTY, and --no-emulator needs something listening.
  local emulator_rapi= rapi_host=host.docker.internal
  if [ "$firmware_mode" = docker ]; then
    emulator_rapi=$rapi_port
    # Both containers: give them a private network and let the firmware reach
    # the emulator by name on its internal port, so the emulator's RAPI stays
    # published on loopback only.
    if [ "$emulator_mode" = docker ]; then
      docker_network="openevse_harness_$id"
      docker network rm "$docker_network" >/dev/null 2>&1 || true
      docker network create "$docker_network" >/dev/null \
        || die "could not create docker network $docker_network"
      networks+=("$docker_network")
      rapi_host="openevse_emulator_$id"
      emulator_rapi=8023
    fi
  fi

  case $emulator_mode in
    docker) start_emulator_docker "$id" "openevse_emulator_$id" "$web_port" "$rapi_port"
            [ "$firmware_mode" = docker ] || bridge_pty_to_tcp "$pty" "$rapi_port" ;;
    local)  start_emulator_local "$id" "$web_port" "$pty" "$emulator_rapi" ;;
    none)   log "no emulator — RAPI never answers, so /status stays state=0"
            if [ "$firmware_mode" = docker ]; then
              start_null_rapi_listener "$rapi_port"
            else
              start_loopback_pty "$pty"
            fi ;;
  esac

  if [ "$firmware_mode" = docker ]; then
    start_native_docker "$id" "$hostname" "$chip_id" "$rapi_host" "$emulator_rapi" \
      "$http_port" "${extra_config[@]}"
  else
    start_native "$id" "$hostname" "$chip_id" "$pty" "$http_port" "$workdir" \
      "${extra_config[@]}"
  fi

  if [ "$emulator_mode" != none ]; then
    wait_for_evse_state "http://localhost:$http_port" 30 \
      || die "instance $id never left state=0 (STARTING) — RAPI is not getting through to the emulator"
  fi

  export "EVSE_PTY_$id=$pty"
  export "EVSE_CHIP_ID_$id=$chip_id"
  [ "$emulator_mode" = none ] || export "EMULATOR_URL_$id=http://localhost:$web_port"
  export_instance_env

  if [ $# -gt 0 ]; then
    "$@"
  else
    log "running — point your REST client at http://localhost:$http_port; Ctrl-C to stop"
    wait_until_interrupted
  fi
}

# Block until Ctrl-C, or until one of the processes we started dies.
wait_until_interrupted() {
  while :; do
    local pid
    for pid in "${pids[@]}"; do
      kill -0 "$pid" 2>/dev/null && continue
      log "a launched process exited — shutting down (logs: $run_dir)"
      return 1
    done
    sleep 1
  done
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
  awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$script_path"
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
  launch)      cmd_launch "$@" ;;
  integration) cmd_integration "$@" ;;
  ""|-h|--help|help) usage ;;
  *) die "unknown command '$command' (try --help)" ;;
esac
