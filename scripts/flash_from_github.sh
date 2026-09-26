#!/usr/bin/env bash
#
# Flash an OpenEVSE ESP32 device straight from a GitHub build, with no local
# checkout or PlatformIO build required. Downloads the requested image with
# the GitHub CLI (`gh`) and writes it with esptool.py (serial), a plain HTTP
# POST to the firmware's own /update endpoint, or espota.py (ArduinoOTA).
#
#   scripts/flash_from_github.sh --port /dev/ttyUSB0
#   scripts/flash_from_github.sh --pr 1027 --env openevse_wifi_v1_16mb --port /dev/ttyUSB0
#   scripts/flash_from_github.sh --branch master --http openevse-ev1.local
#   scripts/flash_from_github.sh --tag v6.2.1 --ota openevse-ev1.local
#
# Source (pick one; default --branch master):
#   --branch NAME   release built from that branch: "master" -> the rolling
#                   "latest" pre-release, anything else -> that tag
#   --tag TAG       a tagged release (e.g. v6.2.1)
#   --pr N          the build artifact from PR #N's own CI run, not a release
#                   (works for same-repo PRs; forked PRs run CI in the base
#                   repo too, so this still finds them)
#   --repo OWNER/REPO   GitHub repo to pull from (default OpenEVSE/openevse_esp32_firmware)
#
# Target (exactly one of):
#   --port PATH     flash over serial with esptool.py
#   --http HOST     POST to http://HOST/update (the firmware's own updater)
#   --ota HOST      push over ArduinoOTA with espota.py (port 3232, no password)
#
#   --env ENV       PlatformIO environment / board (default openevse_wifi_v1)
#   --baud RATE     serial upload baud rate (default 460800)
#   --erase         erase the WHOLE flash before writing (serial only) -- this
#                   wipes the LittleFS partition too, so the device loses its
#                   WiFi credentials and config and comes up as its factory AP
#   --reset-boot-slot   erase just the otadata partition (0xe000, 0x2000 --
#                   the same offset on every partition table this repo ships)
#                   before writing (serial only). This board's OTA-enabled
#                   partition tables keep two full app slots (ota_0/ota_1);
#                   otadata records which one boots, and a plain serial write
#                   only ever touches ota_0, so a device that had booted from
#                   ota_1 keeps booting the OLD image otadata still points at.
#                   This forces a clean boot into the image just written,
#                   without --erase's side effect of wiping WiFi credentials.
#   --bootloader-offset OFFSET   default 0x1000 (0x0 on ESP32-C3 boards)
#   --keep DIR      copy the downloaded images into DIR instead of discarding them
#   --monitor       after flashing, attach to the device's console: a serial
#                   monitor for --port (via `pio device monitor`, falling back
#                   to `python3 -m serial.tools.miniterm`), or the live debug
#                   websocket (ws://HOST/debug/console, needs `websocat`) for
#                   --http/--ota. Ctrl-C (or Ctrl-] for the serial monitor) to
#                   detach.
#   --monitor-baud RATE   serial monitor baud rate (default 115200 -- this
#                   repo's DEBUG_PORT rate, independent of --baud's upload speed)
#   -h, --help      show this help
#
# Release assets only ship a matching bootloader.bin/partitions.bin for the
# boards used by the 16MB flash-size migrator (openevse_wifi_v1,
# openevse_wifi_v1_16mb, openevse_wifi_tft_v1); for any other --env from a
# --branch/--tag source only the application image is written, which is fine
# for updating a device that already has a bootloader and partition table
# flashed. --pr artifacts always include everything the build produced
# (bootloader.bin, partitions.bin, firmware.bin) since CI uploads the whole
# build directory.
#
# Needs the GitHub CLI (`gh`, already logged in) to resolve and download the
# image, and depending on the target: esptool.py, curl, or espota.py. None of
# these need to be on PATH -- if they aren't, this also looks inside
# PlatformIO's own installs ($PLATFORMIO_CORE_DIR, ~/.platformio and
# ~/.platformio-core3 / $PIO_CORE3_DIR -- see scripts/pio), since espota.py in
# particular ships only with the arduino-esp32 core's tools/, not PyPI.
set -euo pipefail

log() { printf '==> %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

arg_value() {
  local flag=$1 value=${2:-}
  [ -n "$value" ] || die "$flag requires a value"
  printf '%s' "$value"
}

is_number() {
  case ${1:-} in
    ''|*[!0-9]*) return 1 ;;
    *) return 0 ;;
  esac
}

usage() {
  awk 'NR == 1 { next } /^#/ { sub(/^# ?/, ""); print; next } { exit }' "$0"
}

# PlatformIO core dirs to search when a tool isn't on PATH, in the order
# scripts/pio itself picks them: an explicit PLATFORMIO_CORE_DIR first, then
# the core-2 default (~/.platformio) and the core-3 dir (~/.platformio-core3,
# or $PIO_CORE3_DIR).
pio_core_dirs() {
  [ -n "${PLATFORMIO_CORE_DIR:-}" ] && printf '%s\n' "$PLATFORMIO_CORE_DIR"
  printf '%s\n' "$HOME/.platformio"
  printf '%s\n' "${PIO_CORE3_DIR:-$HOME/.platformio-core3}"
}

# find_pio_tool RELATIVE_PATH -- prints the first match for RELATIVE_PATH
# under a PlatformIO core dir, or nothing (and fails) if none exists.
find_pio_tool() {
  local rel=$1 dir
  while IFS= read -r dir; do
    [ -x "$dir/$rel" ] && { printf '%s\n' "$dir/$rel"; return 0; }
  done < <(pio_core_dirs)
  return 1
}

repo="OpenEVSE/openevse_esp32_firmware"
branch=master
tag=
pr=
env=openevse_wifi_v1
port=
http_host=
ota_host=
baud=460800
erase=0
reset_boot_slot=0
bootloader_offset=0x1000
partitions_offset=0x8000
app_offset=0x10000
otadata_offset=0xe000
otadata_size=0x2000
keep_dir=
monitor=0
monitor_baud=115200

while [ $# -gt 0 ]; do
  case "$1" in
    --branch)              branch=$(arg_value "$1" "${2:-}"); tag=; pr=; shift 2 ;;
    --tag)                 tag=$(arg_value "$1" "${2:-}"); branch=; pr=; shift 2 ;;
    --pr)                  pr=$(arg_value "$1" "${2:-}"); branch=; tag=; shift 2 ;;
    --repo)                repo=$(arg_value "$1" "${2:-}"); shift 2 ;;
    --env)                 env=$(arg_value "$1" "${2:-}"); shift 2 ;;
    --port)                port=$(arg_value "$1" "${2:-}"); shift 2 ;;
    --http)                http_host=$(arg_value "$1" "${2:-}"); shift 2 ;;
    --ota)                 ota_host=$(arg_value "$1" "${2:-}"); shift 2 ;;
    --baud)                baud=$(arg_value "$1" "${2:-}"); shift 2 ;;
    --erase)               erase=1; shift ;;
    --reset-boot-slot)     reset_boot_slot=1; shift ;;
    --bootloader-offset)   bootloader_offset=$(arg_value "$1" "${2:-}"); shift 2 ;;
    --keep)                keep_dir=$(arg_value "$1" "${2:-}"); shift 2 ;;
    --monitor)             monitor=1; shift ;;
    --monitor-baud)        monitor_baud=$(arg_value "$1" "${2:-}"); shift 2 ;;
    -h|--help)             usage; exit 0 ;;
    *)                     die "unknown option '$1' (see --help)" ;;
  esac
done

[ -n "$pr" ] && ! is_number "$pr" && die "--pr takes a pull request number (got '$pr')"

targets=0
[ -n "$port" ] && targets=$((targets + 1))
[ -n "$http_host" ] && targets=$((targets + 1))
[ -n "$ota_host" ] && targets=$((targets + 1))
[ "$targets" -eq 1 ] || die "pass exactly one of --port, --http, --ota"

[ "$erase" -eq 1 ] && [ "$reset_boot_slot" -eq 1 ] && die "pass at most one of --erase, --reset-boot-slot"
[ "$erase" -eq 1 ] && [ -z "$port" ] && die "--erase only makes sense with --port"
[ "$reset_boot_slot" -eq 1 ] && [ -z "$port" ] && die "--reset-boot-slot only makes sense with --port"

command -v gh >/dev/null 2>&1 || die "the GitHub CLI ('gh') is required; see https://cli.github.com"

workdir=$(mktemp -d "${TMPDIR:-/tmp}/flash_from_github.XXXXXX")
cleanup() { [ -n "$keep_dir" ] || rm -rf "$workdir"; }
trap cleanup EXIT

# Resolves to the .bin files this run will flash, populating $fw_bin,
# $bootloader_bin and $partitions_bin (the latter two may stay empty).
fw_bin=
bootloader_bin=
partitions_bin=

fetch_release() {
  local release_tag=$1
  log "downloading '$env' from $repo release '$release_tag'"
  gh release download "$release_tag" -R "$repo" \
    --pattern "${env}.bin" --dir "$workdir" --clobber \
    || die "no '${env}.bin' asset on release '$release_tag' of $repo -- check --env and --branch/--tag"
  fw_bin="$workdir/${env}.bin"

  # Only these three boards get a matching bootloader/partitions pair
  # published (see .github/workflows/build.yaml's "Get the bootloader and
  # partition files" step); anything else is app-image-only.
  local suffix=
  case "$env" in
    openevse_wifi_v1)       suffix=4mb ;;
    openevse_wifi_v1_16mb)  suffix=v1_16mb ;;
    openevse_wifi_tft_v1)   suffix=16mb ;;
  esac
  if [ -n "$suffix" ]; then
    gh release download "$release_tag" -R "$repo" \
      --pattern "bootloader_${suffix}.bin" --pattern "partitions_${suffix}.bin" \
      --dir "$workdir" --clobber 2>/dev/null || true
    [ -f "$workdir/bootloader_${suffix}.bin" ] && bootloader_bin="$workdir/bootloader_${suffix}.bin"
    [ -f "$workdir/partitions_${suffix}.bin" ] && partitions_bin="$workdir/partitions_${suffix}.bin"
  fi
}

fetch_pr_artifact() {
  local pr_number=$1
  log "looking up the CI run for PR #$pr_number on $repo"
  local head_ref
  head_ref=$(gh pr view "$pr_number" -R "$repo" --json headRefName --jq .headRefName) \
    || die "could not find PR #$pr_number on $repo"

  local run_id
  run_id=$(gh run list -R "$repo" --workflow build.yaml --branch "$head_ref" \
              --json databaseId,event,status --jq \
              '[.[] | select(.event == "pull_request" and .status == "completed")][0].databaseId') \
    || die "could not list workflow runs for $repo"
  [ -n "$run_id" ] && [ "$run_id" != "null" ] \
    || die "no completed build.yaml run found for PR #$pr_number (branch '$head_ref')"

  log "downloading '${env}.bin' artifact from run $run_id"
  gh run download "$run_id" -R "$repo" -n "${env}.bin" -D "$workdir" \
    || die "no '${env}.bin' artifact on run $run_id -- check --env"

  fw_bin="$workdir/firmware.bin"
  [ -f "$fw_bin" ] || die "run $run_id's '${env}.bin' artifact has no firmware.bin"
  [ -f "$workdir/bootloader.bin" ] && bootloader_bin="$workdir/bootloader.bin"
  [ -f "$workdir/partitions.bin" ] && partitions_bin="$workdir/partitions.bin"
}

if [ -n "$pr" ]; then
  fetch_pr_artifact "$pr"
elif [ -n "$tag" ]; then
  fetch_release "$tag"
else
  release_tag=latest
  [ "$branch" = master ] || release_tag="$branch"
  fetch_release "$release_tag"
fi

if [ -n "$keep_dir" ]; then
  mkdir -p "$keep_dir"
  cp "$workdir"/*.bin "$keep_dir"/
  log "downloaded images kept in $keep_dir"
fi

log "flashing $fw_bin"

if [ -n "$port" ]; then
  esptool_bin=$(command -v esptool.py 2>/dev/null) \
    || esptool_bin=$(find_pio_tool "penv/bin/esptool.py") \
    || true
  if [ -n "$esptool_bin" ]; then
    esptool=("$esptool_bin")
  else
    esptool=(python3 -m esptool)
    log "esptool.py not found on PATH or in a PlatformIO install -- trying 'python3 -m esptool'"
  fi

  if [ "$erase" -eq 1 ]; then
    log "erasing the whole flash on $port -- this wipes WiFi credentials and config too"
    "${esptool[@]}" --port "$port" --baud "$baud" erase_flash
  elif [ "$reset_boot_slot" -eq 1 ]; then
    log "erasing otadata on $port to force a clean boot into the image just written"
    "${esptool[@]}" --port "$port" --baud "$baud" erase_region "$otadata_offset" "$otadata_size"
  fi

  write_args=("$app_offset" "$fw_bin")
  if [ -n "$bootloader_bin" ] && [ -n "$partitions_bin" ]; then
    write_args=("$bootloader_offset" "$bootloader_bin" "$partitions_offset" "$partitions_bin" "${write_args[@]}")
  else
    log "no bootloader/partitions image for '$env' -- writing the application image only (fine for updating a device that is already flashed)"
  fi

  "${esptool[@]}" --chip esp32 --port "$port" --baud "$baud" \
    --before default_reset --after hard_reset \
    write_flash -z "${write_args[@]}"

elif [ -n "$http_host" ]; then
  command -v curl >/dev/null 2>&1 || die "curl is required for --http"
  curl -f -F "firmware=@${fw_bin}" "http://${http_host}/update" --progress-bar | cat

else
  espota_bin=$(command -v espota.py 2>/dev/null) \
    || espota_bin=$(find_pio_tool "packages/framework-arduinoespressif32/tools/espota.py") \
    || die "espota.py not found on PATH or in a PlatformIO install (~/.platformio, ~/.platformio-core3) -- it ships with the arduino-esp32 core's tools/ directory, not PyPI"
  "$espota_bin" -i "$ota_host" -p 3232 -f "$fw_bin"
fi

log "done"

if [ "$monitor" -eq 1 ]; then
  if [ -n "$port" ]; then
    log "attaching a serial monitor on $port at $monitor_baud baud (Ctrl-] to exit)"
    if command -v pio >/dev/null 2>&1; then
      pio device monitor -p "$port" -b "$monitor_baud" --raw
    else
      python3 -m serial.tools.miniterm "$port" "$monitor_baud"
    fi
  else
    monitor_host=${http_host:-$ota_host}
    command -v websocat >/dev/null 2>&1 \
      || die "--monitor over the network needs 'websocat' on PATH -- or connect yourself to ws://$monitor_host/debug/console"
    log "attaching to ws://$monitor_host/debug/console (Ctrl-C to exit)"
    websocat "ws://$monitor_host/debug/console"
  fi
fi
