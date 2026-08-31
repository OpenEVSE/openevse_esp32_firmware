# AI agent sandbox

How to run AI coding agents (Claude Code in particular) on this repo with less
permission friction but a real safety boundary — an agent can build, flash, and test
without either constant prompting or the ability to exfiltrate secrets, reach arbitrary
hosts, or clobber the host filesystem.

The policy lives in [`.claude/settings.json`](../../.claude/settings.json) (checked in,
team-shared). It configures Claude Code's built-in **Bash sandbox**, which uses OS
primitives — Seatbelt on macOS, [bubblewrap](https://github.com/containers/bubblewrap)
on Linux/WSL2 — to confine every Bash command and its children. Native Windows is not
supported; use WSL2 or a container.

## Enable it

```bash
# Linux/WSL2 only — macOS needs nothing installed
sudo apt-get install bubblewrap socat
```

The sandbox is enabled by `sandbox.enabled` in the checked-in settings, so it applies as
soon as the config is present — there is no command to run. In a terminal Claude Code
session, `/sandbox` opens a panel to inspect the resolved policy and switch modes (it
writes your mode choice to the gitignored `.claude/settings.local.json`, and shows a
Dependencies tab if `bubblewrap`/`socat` are missing). That panel is not available in
the VS Code extension or SDK harnesses; the policy still applies there.

### Ubuntu 24.04+ — AppArmor user namespace restriction

Ubuntu 24.04 and later ship `kernel.apparmor_restrict_unprivileged_userns=1`, which stops
bubblewrap from creating the user namespaces the sandbox needs. This bites hardest in the
**VS Code extension**, which runs commands through its own bundled sandbox runtime, so
Claude's sandbox has to open a *nested* namespace inside it. The symptom is every Bash
command failing with:

```text
apply-seccomp: write /proc/self/setgroups (nested userns is capability-restricted; ...)
bwrap: No permissions to create a new namespace
```

Note that a *single* namespace can succeed while nesting still fails, so test nesting
specifically:

```bash
bwrap --ro-bind / / --proc /proc --dev /dev --unshare-user --unshare-pid -- \
  bwrap --ro-bind / / --proc /proc --unshare-user true && echo OK
```

If that fails, install a `bwrap` AppArmor profile granting the `userns` capability:

```bash
sudo tee /etc/apparmor.d/bwrap > /dev/null <<'EOF'
abi <abi/4.0>,
include <tunables/global>

profile bwrap /usr/bin/bwrap flags=(unconfined) {
  userns,
  include if exists <local/bwrap>
}
EOF
sudo apparmor_parser -r -W /etc/apparmor.d/bwrap
```

The profile applies only to `bwrap` itself, not to the commands it runs inside the
sandbox. Re-run the nesting test to confirm, then restart Claude Code. Verified on Ubuntu
26.04 / kernel 7.0.

### Optional: git push over SSH from inside the sandbox

The sandbox already routes SSH through its proxy (it sets a `GIT_SSH_COMMAND` with a
`socat ... PROXY:` wrapper), so `github.com` over SSH works on the network side. What
blocks it is authentication: the policy denies `~/.ssh`, and the ssh-agent socket is
blocked like any other Unix socket. Without this, `git push` falls back to running
outside the sandbox and prompts each time.

Prefer the **ssh-agent** over exposing a key file — the agent only ever returns
signatures, never private key material, so no key becomes readable. Because the socket
path is specific to your UID and agent, put it in the gitignored
`.claude/settings.local.json`, not the shared policy:

```json
{
  "sandbox": {
    "network": {
      "allowUnixSockets": ["/run/user/1000/gcr/ssh"]
    },
    "filesystem": {
      "allowRead": ["~/.ssh/known_hosts"]
    }
  }
}
```

Use your own `echo $SSH_AUTH_SOCK` for the path. The `known_hosts` entry re-opens one
file inside the denied `~/.ssh`, so host verification works without exposing any keys;
everything else in `~/.ssh` stays blocked. Restart Claude Code, then confirm with
`ssh -T git@github.com` (expect "successfully authenticated") and `ssh-add -l`.

Only allow the ssh-agent socket. Allowing sockets broadly — `/var/run/docker.sock`
especially — hands over host access and defeats the sandbox, and `allowAllUnixSockets`
should stay off.

## What the policy does

- **Network egress allowlist** (`sandbox.network.allowedDomains`) — sandboxed commands
  can only reach the domains the build actually needs: PlatformIO registry, GitHub
  (toolchain + git deps), npm, PyPI, GHCR (integration-test emulator image), Playwright
  browser CDNs, and `api.anthropic.com`. Everything else is blocked. No domains are
  allowed by default, so this list is the whole surface.
- **Persistent toolchain caches** (`sandbox.filesystem.allowWrite`) — `~/.platformio`
  and `~/.platformio-core3` stay writable so the ~500 MB toolchain (and the core-3
  variant) isn't re-downloaded each session. The project `.pio/` dir is already writable
  as the working directory, which matters because the build hook
  [`scripts/extra_script.py`](../../scripts/extra_script.py) patches files inside
  `.pio/libdeps/` on every build.
- **Credential protection** (`sandbox.credentials` + `filesystem.denyRead`) — reads of
  `~/.ssh` and `~/.aws` are blocked, and `DEPENDABOT_PAT`, `OTA_SIGNING_KEY`,
  `GITHUB_TOKEN`, `NPM_TOKEN` are unset for sandboxed commands.
- **Read denials for on-disk secrets** (`permissions.deny`) — the built-in Read/Edit
  tools are *not* covered by the Bash sandbox, so `*.pem` (OTA signing keys), `.env`
  files, and `.vscode/settings.json` (holds test Wi-Fi creds) are denied directly.
- **Bounded-depth deny globs, deliberately** — the secret denials use explicit
  `./*/`, `./*/*/` levels rather than a recursive `./**/`. Path patterns are resolved
  against the real tree, and that resolution follows symlinks. `lib/` symlinks point at
  sibling checkouts (`../../ArduinoMongoose` etc.) whose `examples/*/lib/` directories
  symlink *back* to their own root, forming cycles. A recursive `**` walk never
  terminates on that shape — it generates paths until the process is OOM-killed (observed:
  ~20 GB RSS in the V3.x tree, which has these `lib/` symlinks). Every real secret lives
  at depth ≤ 3, so bounding the depth costs no coverage. **Do not change these back to
  `**`.**
- **Prompts on network-mutating actions** (`permissions.ask`) — `git push` and the
  device OTA flash (`curl ... /update`) still surface a confirmation even in auto-allow.
- **docker escape hatch** (`sandbox.excludedCommands`) — `docker` is incompatible with
  the sandbox and is used by the integration tests, so it runs outside the sandbox via
  the normal permission flow. The emulator-backed harness entry points
  (`openevse_test.sh emulator`, `openevse_test.sh launch`, `openevse_test.sh integration`,
  `pytest tests/integration`) are excluded for the same reason — see the section below.
- **Test and diagnostic commands** (`permissions.allow`) — the test runners
  (`pio test`, `pytest`, `npm test`), the native/simulator host binaries, `socat`, and
  the port-inspection tools (`lsof`, `fuser`) are allowlisted so a full test cycle runs
  without prompts. `~/.npm` is writable so `npm install`/`npm ci` can populate the cache.

## Running tests and multiple instances

Use [`scripts/openevse_test.sh`](../../scripts/openevse_test.sh) rather than assembling
emulator plumbing by hand:

```bash
scripts/openevse_test.sh unit | divert | gui | all
scripts/openevse_test.sh native -n 2                    # 2 firmware instances, sandboxed
scripts/openevse_test.sh native -- curl -s "$EVSE_URL/status"
scripts/openevse_test.sh emulator -n 2                  # firmware + emulator pairs
scripts/openevse_test.sh integration                    # the checked-in pytest suite
scripts/openevse_test.sh launch -i 1                    # one pair, held up until Ctrl-C
```

Instances are numbered from 0 and use the same port bases as
`tests/integration/conftest.py` — firmware on `8000+i`, emulator web on `8080+i`, emulator
RAPI on `8023+i`. The wrapped command gets `EVSE_URL`, `EVSE_URL_<i>`, `EVSE_URLS`,
`EVSE_COUNT`, `EVSE_PTY_<i>`, and (for `emulator`) `EMULATOR_URL_<i>`.

### `launch` — a single pair for interactive work

`launch` is the one subcommand meant to outlive its own startup: it brings up one emulator
plus one firmware instance for a given instance ID and blocks until Ctrl-C, so a VS Code
REST client (`test/*.http`) or a second terminal can drive it. It only works from a real
terminal, not from a sandboxed agent Bash call, for the namespace reason below.

Everything is derived from `-i ID` and can be overridden individually:

| default for instance `i`     | override            |
| ---------------------------- | ------------------- |
| firmware HTTP `8000+i`       | `--http-port`       |
| emulator web `8080+i`        | `--web-port`        |
| emulator RAPI TCP `8023+i`   | `--rapi-port`       |
| PTY in the run dir           | `--rapi-serial PATH`|
| chip ID `0x1234567890ABCDEF-i` | `--chip-id HEX`   |
| hostname `openevse-ev<i>`    | `--hostname NAME`   |
| working directory `test/<i>` | `--workdir DIR`     |

Anything else the firmware understands can be passed through with a repeatable
`--set-config NAME=VALUE`.

The firmware console is mirrored to the terminal as `[fw<i>] …`; the emulator console
(`[emu<i>] …`) is off by default because it logs every RAPI poll. Toggle either with
`--[no-]firmware-console` / `--[no-]emulator-console`, or both at once with `--console` /
`--no-console`. Either way both consoles are always written to their log file under the run
directory, whose path is printed at startup.

The chip ID reaches the firmware as `OPENEVSE_CHIP_ID` (read by ESPAL's EpoxyDuino HAL) so
each peer in a load-sharing group has a distinct identity, and the per-instance working
directory keeps EEPROM/LittleFS state separate and persistent across runs.

The emulator runs from the Docker image by default, bridged to the firmware's PTY with
`socat`. `--local` (or `--emulator-dir DIR`, default `../OpenEVSE_Emulator`, env
`OPENEVSE_EMULATOR_DIR`) runs it from a source checkout instead, in which case the emulator
creates the PTY itself and neither Docker nor `socat` is involved. `--no-emulator` gives a
firmware-only instance on a loopback PTY — no Docker, so it also works sandboxed, at the
cost of RAPI never answering (`/status` stays `state=0`).

```bash
# two terminals, two peers, Docker emulators
scripts/openevse_test.sh launch -i 0
scripts/openevse_test.sh launch -i 1

# emulator from a source checkout, non-default firmware port
scripts/openevse_test.sh launch -i 1 --local --http-port 9001
```

### Why everything happens in one command

Each sandboxed Bash invocation runs in its **own network namespace**. A server started in
one call is not reachable from the next, and `run_in_background` does not help — the
listener dies with its namespace. So there is no "start the emulator, then poke it" flow:
every subcommand launches what it needs, runs your command, and tears down on exit
(processes killed, containers removed, temp dir deleted, even on failure).

### What needs to leave the sandbox, and why

`native` works fully sandboxed. The emulator paths do not, for two independent reasons:

- The **docker socket is blocked** — it is a Unix socket, and allowing it would hand over
  host access (`allowUnixSockets` is macOS-only in any case).
- **Host-published container ports are unreachable** from inside the sandbox. A sandboxed
  `curl` to a published port returns status `000` while the same request succeeds on the
  host, because the namespace has no route to the host's loopback.

`/tmp` is also read-only inside the sandbox, which is why the harness uses `$TMPDIR` for
its PTYs — but `tests/integration/conftest.py` hardcodes `/tmp/rapi_pty_<n>`, one more
reason that suite runs outside.

Sandboxed `native` instances get a loopback PTY with nothing on the far end, so RAPI never
answers and `/status` stays `state: 0` (STARTING). Config, claims, and override
diagnostics work; anything asserting on real EVSE state needs `emulator` or `integration`.

## Caveats

- The proxy allows by hostname and does **not** inspect TLS by default, so a broad
  domain like `github.com` is still a possible exfiltration path (domain fronting). Keep
  the allowlist as narrow as the build tolerates.
- The sandbox confines Bash only. MCP servers and hooks run unconfined on the host.
- Only HTTP/HTTPS go through the proxy's domain allowlist. SSH is tunnelled via
  `GIT_SSH_COMMAND`, and other protocols are simply blocked — so an allowlisted domain is
  not automatically reachable on every port.
- For unattended / `--dangerously-skip-permissions` runs, this host-level sandbox is not
  enough — use a dev container with a default-deny egress firewall (the Anthropic
  reference container) or a VM. That is a deliberate follow-up, not set up here.

## Verify

```bash
scripts/openevse_test.sh all     # unit + divert + gui, all sandboxed
scripts/openevse_test.sh native -n 2   # two firmware instances, sandboxed
scripts/openevse_test.sh integration   # emulator-backed, runs outside the sandbox
pio run -e native_openevse       # native firmware binary builds sandboxed
curl https://example.com         # should be BLOCKED/prompted — proves the allowlist
cat ~/.ssh/id_* 2>&1             # should be denied inside a sandboxed command
curl -s --max-time 3 http://localhost:8080/api/status   # 000 while an emulator
                                 # container is up — proves published container
                                 # ports are unreachable from the sandbox
```
