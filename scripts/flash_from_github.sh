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
#   --http-user USER:PASS   HTTP Basic credentials for --http, needed when the
#                   device has a web username/password set (otherwise /update
#                   answers 401). Can also be given via $OPENEVSE_HTTP_USER
#                   to keep the password out of your shell history
#   --ota HOST      push over ArduinoOTA with espota.py (port 3232, no password)
#
#   --env ENV       PlatformIO environment / board (default openevse_wifi_v1)
#   --baud RATE     serial upload baud rate (default 460800)
#   --with-bootloader   fetch a matching bootloader+partitions pair for --env
#                   from the same branch/tag/PR's own CI run, even when the
#                   release doesn't ship one (see below) -- implied by --erase,
#                   which needs one to avoid leaving the device unbootable
#   --erase         erase the WHOLE flash before writing (serial only) -- this
#                   wipes the LittleFS partition too, so the device loses its
#                   WiFi credentials and config and comes up as its factory AP.
#                   Implies --with-bootloader; refuses to run if no matching
#                   bootloader+partitions can be found anywhere
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
# --branch/--tag source, by default only the application image is written,
# which is fine for updating a device that already has a bootloader and
# partition table flashed. --with-bootloader (or --erase) instead looks up
# the CI run that built that branch/tag/PR and downloads its "$env.bin"
# artifact, which CI uploads for every matrix env and always has all three
# files -- the same thing --pr already does by default. This needs the run's
# artifacts to still exist (GitHub's default retention is 90 days), so it can
# fail for an old tag even though the release asset itself is still there.
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
http_user=${OPENEVSE_HTTP_USER:-}
ota_host=
baud=460800
erase=0
with_bootloader=0
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
    --http-user)           http_user=$(arg_value "$1" "${2:-}"); shift 2 ;;
    --ota)                 ota_host=$(arg_value "$1" "${2:-}"); shift 2 ;;
    --baud)                baud=$(arg_value "$1" "${2:-}"); shift 2 ;;
    --with-bootloader)     with_bootloader=1; shift ;;
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
[ "$with_bootloader" -eq 1 ] && [ -z "$port" ] && die "--with-bootloader only makes sense with --port"
[ -n "$http_user" ] && [ -z "$http_host" ] && die "--http-user only makes sense with --http"
[ "$reset_boot_slot" -eq 1 ] && [ -z "$port" ] && die "--reset-boot-slot only makes sense with --port"

command -v gh >/dev/null 2>&1 || die "the GitHub CLI ('gh') is required; see https://cli.github.com"

workdir=$(mktemp -d "${TMPDIR:-/tmp}/flash_from_github.XXXXXX")
cleanup() { rm -rf "$workdir"; }
trap cleanup EXIT

# Resolves to the .bin files this run will flash, populating $fw_bin,
# $bootloader_bin and $partitions_bin (the latter two may stay empty).
fw_bin=
bootloader_bin=
partitions_bin=

fetch_release() {
  local release_tag=$1
  case "$env" in
    *_dev) log "WARNING: CI deliberately drops _dev builds from releases (build.yaml's \"Drop the _dev images from the release\" step) -- if '${env}.bin' exists on '$release_tag' anyway it's a stale leftover, not something this repo intends to publish. Use --pr for a _dev build instead." ;;
  esac
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

# find_ci_run FILTER_FLAG FILTER_VALUE EVENT -- prints the databaseId of the
# most recent successful build.yaml run matching FILTER_FLAG/FILTER_VALUE
# (--branch NAME or --commit SHA) and EVENT ("push" or "pull_request"), all
# filtered server-side so a long run history can't hide an older match behind
# gh's default 20-run page, or nothing (and fails) if there isn't one.
find_ci_run() {
  local filter_flag=$1 filter_value=$2 event=$3 run_id
  run_id=$(gh run list -R "$repo" --workflow build.yaml \
              "$filter_flag" "$filter_value" --event "$event" --status success \
              --json databaseId --jq '.[0].databaseId') \
    || return 1
  [ -n "$run_id" ] && [ "$run_id" != "null" ] || return 1
  printf '%s\n' "$run_id"
}

fetch_pr_artifact() {
  local pr_number=$1
  log "looking up the CI run for PR #$pr_number on $repo"
  local head_sha
  head_sha=$(gh pr view "$pr_number" -R "$repo" --json headRefOid --jq .headRefOid) \
    || die "could not find PR #$pr_number on $repo"

  local run_id
  run_id=$(find_ci_run --commit "$head_sha" pull_request) \
    || die "no completed, successful build.yaml run found for PR #$pr_number's current head ($head_sha) -- CI may still be running, or the PR needs a rebuild"

  log "downloading '${env}.bin' artifact from run $run_id"
  gh run download "$run_id" -R "$repo" -n "${env}.bin" -D "$workdir" \
    || die "no '${env}.bin' artifact on run $run_id -- check --env"

  fw_bin="$workdir/firmware.bin"
  [ -f "$fw_bin" ] || die "run $run_id's '${env}.bin' artifact has no firmware.bin"
  [ -f "$workdir/bootloader.bin" ] && bootloader_bin="$workdir/bootloader.bin"
  [ -f "$workdir/partitions.bin" ] && partitions_bin="$workdir/partitions.bin"
}

# fetch_bootloader_partitions_for_ref REF EVENT -- looks up REF's own CI run
# and, if found, downloads its "$env.bin" artifact for a matching
# bootloader+partitions (and, for consistency, uses its firmware.bin too, so
# all three come from the exact same build). Non-fatal: leaves bootloader_bin/
# partitions_bin/fw_bin untouched and returns 1 if the run or artifact isn't
# there (e.g. its retention window has expired) -- the caller decides whether
# that's acceptable.
fetch_bootloader_partitions_for_ref() {
  local ref=$1 event=$2 run_id bp_dir
  log "looking up the CI run for '$ref' to fetch a matching bootloader+partitions for '$env'"
  run_id=$(find_ci_run --branch "$ref" "$event") || {
    log "no completed, successful build.yaml run found for '$ref' -- can't fetch a bootloader+partitions this way"
    return 1
  }

  bp_dir="$workdir/bootloader_partitions"
  mkdir -p "$bp_dir"
  gh run download "$run_id" -R "$repo" -n "${env}.bin" -D "$bp_dir" 2>/dev/null || {
    log "no '${env}.bin' artifact on run $run_id -- can't fetch a bootloader+partitions this way"
    return 1
  }

  if [ ! -f "$bp_dir/bootloader.bin" ] || [ ! -f "$bp_dir/partitions.bin" ]; then
    log "run $run_id's '${env}.bin' artifact has no bootloader/partitions"
    return 1
  fi

  bootloader_bin="$bp_dir/bootloader.bin"
  partitions_bin="$bp_dir/partitions.bin"
  [ -f "$bp_dir/firmware.bin" ] && fw_bin="$bp_dir/firmware.bin"
}

if [ -n "$pr" ]; then
  fetch_pr_artifact "$pr"
elif [ -n "$tag" ]; then
  fetch_release "$tag"
  ci_ref=$tag
else
  release_tag=latest
  [ "$branch" = master ] || release_tag="$branch"
  fetch_release "$release_tag"
  ci_ref=$branch
fi

# --pr already gets bootloader/partitions from the same CI run by default, so
# this only has work to do for --branch/--tag.
if [ -z "$pr" ] && { [ "$erase" -eq 1 ] || [ "$with_bootloader" -eq 1 ]; } \
   && { [ -z "$bootloader_bin" ] || [ -z "$partitions_bin" ]; }; then
  fetch_bootloader_partitions_for_ref "$ci_ref" push || true
fi

if [ -n "$keep_dir" ]; then
  mkdir -p "$keep_dir"
  cp "$workdir"/*.bin "$keep_dir"/
  log "downloaded images kept in $keep_dir"
fi

# --erase wipes the bootloader and partition table along with everything
# else; writing only the app image back afterwards leaves the ROM bootloader
# with nothing valid to boot -- not the recoverable "factory AP" state a
# wiped config leaves, but a device with no boot code at all. --with-bootloader
# is an explicit promise of its own that both images will be written. Refuse
# either case rather than silently falling back to an app-only flash.
if { [ "$erase" -eq 1 ] || [ "$with_bootloader" -eq 1 ]; } \
   && { [ -z "$bootloader_bin" ] || [ -z "$partitions_bin" ]; }; then
  die "no bootloader+partitions image could be found for '$env' (including via its own CI run), which --erase/--with-bootloader require -- writing only the app after a full erase leaves the device with no bootloader at all. Try --pr for a build whose CI artifacts still exist, or drop --erase and use --reset-boot-slot instead."
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

  # No --chip: esptool auto-detects it over the serial connection (already
  # relied on above for erase_flash/erase_region), which is what actually
  # supports non-ESP32 --bootloader-offset targets like ESP32-C3.
  "${esptool[@]}" --port "$port" --baud "$baud" \
    --before default_reset --after hard_reset \
    write_flash -z "${write_args[@]}"

  # This always writes to ota_0 but never touches otadata, so a device that
  # last booted ota_1 (this repo's partition tables give every OTA-capable
  # board two full app slots) will keep booting that OLD image afterwards --
  # esptool has no portable way to read otadata's current selection first to
  # warn only when it matters, so just say so unconditionally.
  if [ "$erase" -ne 1 ] && [ "$reset_boot_slot" -ne 1 ]; then
    log "if the device doesn't come up on this build, it may have booted the OTHER OTA slot -- rerun with --reset-boot-slot"
  fi

elif [ -n "$http_host" ]; then
  command -v curl >/dev/null 2>&1 || die "curl is required for --http"
  # Credentials go in via a curl config on stdin so the password never shows
  # up in the process list.
  curl_auth=()
  [ -n "$http_user" ] && curl_auth=(-K -)
  { [ -n "$http_user" ] && printf 'user = "%s"\n' "$(printf '%s' "$http_user" | sed 's/[\\"]/\\&/g')"; :; } |
    curl -f "${curl_auth[@]}" -F "firmware=@${fw_bin}" "http://${http_host}/update" --progress-bar | cat

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
    # websocat wants the credential already base64-encoded, and reads it from
    # WEBSOCAT_BASIC_AUTH (websocat 1.14+) so it stays out of the process list.
    if [ -n "$http_user" ]; then
      command -v base64 >/dev/null 2>&1 || die "base64 is required for --http-user with --monitor"
      WEBSOCAT_BASIC_AUTH=$(printf '%s' "$http_user" | base64 | tr -d '\n') \
        websocat "ws://$monitor_host/debug/console"
    else
      websocat "ws://$monitor_host/debug/console"
    fi
  fi
fi
