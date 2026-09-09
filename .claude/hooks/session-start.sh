#!/bin/bash
# SessionStart hook for Claude Code on the web.
#
# Mirrors the setup steps from .github/workflows/build.yaml and
# docs/ai/sandbox.md so a fresh cloud session can run `pio run`/`pio test`
# without manual bootstrapping. Only runs remotely -- a local dev machine
# is expected to already have these installed.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

cd "$CLAUDE_PROJECT_DIR"

# Claude Code's cloud sessions run as root, but fall back to sudo (and fail
# loudly rather than mysteriously) in case that ever isn't true.
if [ "$(id -u)" = "0" ]; then
  APT="apt-get"
elif command -v sudo >/dev/null 2>&1; then
  APT="sudo apt-get"
else
  echo "session-start.sh: need root or sudo to apt-get install build deps" >&2
  exit 1
fi

# Native firmware build/test needs Avahi (mDNS) and OpenSSL headers.
$APT update -qq
$APT install -y -qq libavahi-client-dev libavahi-common-dev libssl-dev

# Pinned to match .github/workflows/build.yaml: PlatformIO Core 6.2.0 bumped
# its bundled SCons to 4.11.1, which fails every core-3.x (pioarduino,
# arduino+espidf hybrid) env at the final link step. 6.1.19 is the last
# release on the working SCons 4.8.1.
#
# `python3 -m pip` (not bare `pip`, matching .github/workflows/build.yaml)
# so this always installs into the interpreter that ends up on PATH, even on
# images with multiple Pythons where `pip` and `python3` can resolve to
# different installs.
python3 -m pip install --quiet --upgrade "platformio==6.1.19"

# gui-nightshift (web UI), gui-v2 (legacy UI) and migrator submodules.
git submodule update --init --recursive

if [ -d gui-nightshift ]; then
  (cd gui-nightshift && npm install)
fi

if [ -f divert_sim/requirements.txt ]; then
  python3 -m pip install --quiet -r divert_sim/requirements.txt
fi
