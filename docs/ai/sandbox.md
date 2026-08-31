# AI agent sandbox

How to run AI coding agents (Claude Code in particular) on this repo with less
permission friction but a real safety boundary — an agent can build, flash, and test
without either constant prompting or the ability to exfiltrate secrets, reach arbitrary
hosts, or clobber the host filesystem.

The sandbox is Claude Code's built-in **Bash sandbox**, which uses OS primitives —
Seatbelt on macOS, [bubblewrap](https://github.com/containers/bubblewrap) on Linux/WSL2 —
to confine every Bash command and its children. Native Windows is not supported; use
WSL2 or a container.

## What is checked in, and what is not

The configuration is deliberately split in two:

- [`.claude/settings.json`](../../.claude/settings.json) is checked in and applies to
  everyone. It holds only the portable parts: the `deny` list for on-disk secrets and the
  `ask` list for network-mutating commands. Neither needs a sandbox, neither assumes
  anything about your machine, and neither changes your security posture — they only add
  friction in front of things you probably want friction in front of.
- **The sandbox itself is opt-in**, and lives in your gitignored
  `.claude/settings.local.json`. Turning the sandbox on decides how much autonomy an agent
  has on *your* machine, so the repo does not make that call for you. [Enable it](#enable-it)
  below has a starting point to paste in.

The split matters because the sandbox config is not free to adopt. It restricts network
egress to a fixed list of domains, so anyone behind a corporate proxy or a PyPI/npm mirror
that is not on the list gets build failures that do not obviously point back at this
config. Ubuntu 24.04+ users need the [AppArmor workaround](#ubuntu-2404--apparmor-user-namespace-restriction)
as well. And it is Claude-Code-only — contributors using Copilot, Codex or Cursor get
nothing from it, while [`AGENTS.md`](../../AGENTS.md) and
[`docs/ai/invariants.md`](invariants.md) serve all of them.

Note also what the egress allowlist does *not* buy you: `github.com` and
`api.anthropic.com` have to be reachable for the agent to work at all, so a determined
exfiltration path stays open. Treat the allowlist as a guard against accidental reach and
dependency confusion, not as a containment boundary.

## Enable it

```bash
# Linux/WSL2 only — macOS needs nothing installed
sudo apt-get install bubblewrap socat
```

Then paste the block below into `.claude/settings.local.json` (gitignored, per-developer).
It is a starting point, not a fixed policy — trim the `allow` list to the commands you
actually run, and add any domain your mirror or proxy needs.

```json
{
  "$schema": "https://json.schemastore.org/claude-code-settings.json",
  "permissions": {
    "allow": [
      "Bash(curl -s --max-time 5 localhost:80*)",
      "Bash(curl -fsS --max-time 5 localhost:80*)",
      "Bash(python3 -m json.tool)",
      "Bash(pio run *)",
      "Bash(pio test *)",
      "Bash(pio check *)",
      "Bash(pio pkg *)",
      "Bash(scripts/openevse_test.sh:*)",
      "Bash(./scripts/openevse_test.sh:*)",
      "Bash(pytest:*)",
      "Bash(python3 -m pytest:*)",
      "Bash(.venv/bin/pytest:*)",
      "Bash(npm test:*)",
      "Bash(npm run test:*)",
      "Bash(npm run build:*)",
      "Bash(npm run lint:*)",
      "Bash(npm ci:*)",
      "Bash(npx vitest:*)",
      "Bash(socat:*)",
      "Bash(lsof -i:*)",
      "Bash(fuser:*)",
      "Bash(.pio/build/native_openevse/program:*)",
      "Bash(.pio/build/native_simulator/program:*)",
      "Bash(divert_sim/divert_sim:*)",
      "Bash(python scripts/docs_coverage.py:*)",
      "Bash(python scripts/sync_screenshots.py:*)",
      "Bash(git submodule status)"
    ]
  },
  "sandbox": {
    "enabled": true,
    "autoAllowBashIfSandboxed": true,
    "network": {
      "allowedDomains": [
        "api.anthropic.com",
        "*.platformio.org",
        "github.com",
        "*.githubusercontent.com",
        "objects.githubusercontent.com",
        "codeload.github.com",
        "registry.npmjs.org",
        "pypi.org",
        "files.pythonhosted.org",
        "ghcr.io",
        "*.pkg.github.com",
        "playwright.azureedge.net",
        "cdn.playwright.dev"
      ]
    },
    "filesystem": {
      "allowWrite": [
        "~/.platformio",
        "~/.platformio-core3",
        "~/.cache",
        "~/.npm"
      ],
      "denyRead": [
        "~/.ssh",
        "~/.aws"
      ]
    },
    "credentials": {
      "files": [
        { "path": "~/.ssh", "mode": "deny" },
        { "path": "~/.aws/credentials", "mode": "deny" }
      ],
      "envVars": [
        { "name": "DEPENDABOT_PAT", "mode": "deny" },
        { "name": "OTA_SIGNING_KEY", "mode": "deny" },
        { "name": "GITHUB_TOKEN", "mode": "deny" },
        { "name": "NPM_TOKEN", "mode": "deny" }
      ]
    },
    "excludedCommands": [
      "docker *",
      "*openevse_test.sh emulator*",
      "*openevse_test.sh launch*",
      "*openevse_test.sh integration*",
      "*pytest tests/integration*"
    ]
  }
}
```

In a terminal Claude Code session, `/sandbox` opens a panel to inspect the resolved policy
and switch modes (it writes your mode choice to the same
`.claude/settings.local.json`, and shows a Dependencies tab if `bubblewrap`/`socat` are
missing). That panel is not available in the VS Code extension or SDK harnesses; the
policy still applies there.

Claude Code's settings schema moves fairly quickly, and renamed or removed keys degrade
silently rather than erroring — a stale block can look protective while doing nothing. If
you paste this and later find prompts appearing where they did not before, check the
current schema rather than assuming the config is still in force.

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
signatures, never private key material, so no key becomes readable. The socket path is
specific to your UID and agent, so this is per-developer too — merge it into the same
`.claude/settings.local.json` as the block above:

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

The first two bullets are the checked-in half, in
[`.claude/settings.json`](../../.claude/settings.json); they apply to everyone and need no
sandbox. The rest describe the opt-in block above, and only apply once you have pasted it
into `.claude/settings.local.json`.

- **Denials for on-disk secrets** (`permissions.deny`, checked in) — the built-in file
  tools are *not* covered by the Bash sandbox, so `*.pem` (OTA signing keys), `.env`
  files, and `.vscode/settings.json` (holds test Wi-Fi creds) are denied directly. Each
  glob is listed for `Read`, `Edit` and `Write`: `Edit` is in practice already gated by
  its own requirement to `Read` first, but `Write` is not, so without the third verb a
  signing key could still be overwritten.
  - **Bounded-depth deny globs, deliberately** — the secret denials use explicit
    `./*/`, `./*/*/` levels rather than a recursive `./**/`. Path patterns are resolved
    against the real tree, and that resolution follows symlinks. `lib/` symlinks point at
    sibling checkouts (`../../ArduinoMongoose` etc.) whose `examples/*/lib/` directories
    symlink *back* to their own root, forming cycles. A recursive `**` walk never
    terminates on that shape — it generates paths until the process is OOM-killed
    (observed: ~20 GB RSS in the V3.x tree, which has these `lib/` symlinks). Every real
    secret lives at depth ≤ 3, so bounding the depth costs no coverage. **Do not change
    these back to `**`.**
- **Prompts on network-mutating actions** (`permissions.ask`, checked in) — `git push` and
  the device OTA flash (`curl ... /update`) still surface a confirmation even in
  auto-allow.
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
`tests/integration/conftest.py` — firmware on `8000+i`, firmware HTTPS on `8443+i`,
emulator web on `8080+i`, emulator RAPI on `8023+i`. The instance number also derives the
firmware hostname (`openevse-ev<i>`) and its chip id, which is what makes several instances
addressable at once — needed for the load sharing work in
[openevse_esp32_firmware#1027](https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1027),
where members join a group by mDNS name.

The wrapped command gets `EVSE_URL`, `EVSE_URL_<i>`, `EVSE_URLS`, `EVSE_NAME_<i>`,
`EVSE_NAMES`, `EVSE_COUNT`, `EVSE_PTY_<i>`, and (for `emulator` and `launch`)
`EMULATOR_URL_<i>`.

### How the chip id is varied

The chip id is set through `OPENEVSE_CHIP_ID`, which the EpoxyDuino `ESPAL` HAL reads.
ESPAL treats it as a MAC: `getLongId()` reverses the bytes and shifts right by 16, and
`getShortId()` keeps the last four hex digits of that. Those digits come from **bytes 2-3**
of the chip id, so varying the low byte would leave every instance sharing a short id — and
with it the default hostname, the softAP SSID and the MQTT announce topic. The harness
varies the `0x5678` field instead (`0x1234<5678+i>90ABCDEF`), leaving instance 0 on the
firmware's own default of `0x1234567890ABCDEF`:

| instance | chip id              | short id |
| -------- | -------------------- | -------- |
| 0        | `0x1234567890ABCDEF` | `7856`   |
| 1        | `0x1234567990ABCDEF` | `7956`   |
| 2        | `0x1234567A90ABCDEF` | `7a56`   |

The *EVSE*'s chip id is a different thing — it comes from the emulator's `$GI` reply and is
hardcoded there, so `/config`'s `chip_id` is the same for every instance.

### `launch` — a single pair for interactive work

`launch` is the one subcommand meant to outlive its own startup: it brings up one emulator
plus one firmware instance for a given instance ID and blocks until Ctrl-C, so a VS Code
REST client (`test/*.http`) or a second terminal can drive it. It only works from a real
terminal, not from a sandboxed agent Bash call, for the namespace reason below.

Everything is derived from `-i ID` and can be overridden individually:

| default for instance `i`         | override             |
| -------------------------------- | -------------------- |
| firmware HTTP `8000+i`           | `--http-port`        |
| firmware HTTPS `8443+i`          | (derived)            |
| emulator web `8080+i`            | `--web-port`         |
| emulator RAPI TCP `8023+i`       | `--rapi-port`        |
| PTY in the run dir               | `--rapi-serial PATH` |
| chip ID `0x1234<5678+i>90ABCDEF` | `--chip-id HEX`      |
| hostname `openevse-ev<i>`        | `--hostname NAME`    |
| working directory `test/<i>`     | `--workdir DIR`      |

Anything else the firmware understands can be passed through with a repeatable
`--set-config NAME=VALUE`.

The firmware console is mirrored to the terminal as `[fw<i>] …`; the emulator console
(`[emu<i>] …`) is off by default because it logs every RAPI poll. Toggle either with
`--[no-]firmware-console` / `--[no-]emulator-console`, or both at once with `--console` /
`--no-console`. Either way both consoles are always written to their log file under the run
directory, whose path is printed at startup.

The chip ID reaches the firmware as `OPENEVSE_CHIP_ID` (read by ESPAL's EpoxyDuino HAL) so
each peer in a load-sharing group has a distinct identity — see
[How the chip id is varied](#how-the-chip-id-is-varied) for why the default is not simply
`base - i`. The per-instance working directory keeps EEPROM/LittleFS state separate and
persistent across runs.

The emulator runs from the Docker image by default, bridged to the firmware's PTY with
`socat`. `--local` (or `--emulator-dir DIR`, default `../OpenEVSE_Emulator`, env
`OPENEVSE_EMULATOR_DIR`) runs it from a source checkout instead, in which case the emulator
creates the PTY itself and neither Docker nor `socat` is involved. `--no-emulator` gives a
firmware-only instance on a loopback PTY — no Docker, so it also works sandboxed, at the
cost of RAPI never answering (`/status` stays `state=0`).

### Running someone else's firmware build

The firmware is your local native build unless you ask otherwise. `--firmware docker` runs
it from the image built by
[`.github/workflows/native_docker.yaml`](../../.github/workflows/native_docker.yaml)
instead, which is how you try a change without building it:

| flag                   | image                                           |
| ---------------------- | ----------------------------------------------- |
| `--pr N`               | `ghcr.io/openevse/openevse-wifi-native:pr-N`    |
| `--firmware-tag TAG`   | the same repo at `TAG` (`latest`, a version)    |
| `--firmware-image REF` | any image reference, e.g. one you built locally |

```bash
# review a PR by running it, with an emulator behind it
scripts/openevse_test.sh launch -i 1 --pr 1027

# the released build, or your own image
scripts/openevse_test.sh launch -i 1 --firmware-tag latest
scripts/openevse_test.sh launch -i 1 --firmware-image openevse-native:wip
```

The repo default is overridable with `OPENEVSE_NATIVE_IMAGE_REPO` / `OPENEVSE_NATIVE_IMAGE`.
The image is public, so `docker pull` needs no login and no `gh`.

**PR images exist only for branches in this repo.** A fork's `GITHUB_TOKEN` is read-only, so
its build cannot push — publishing those would need `pull_request_target`, which runs
untrusted code with a write token and is not worth it. `--pr N` on a fork PR fails with a
message saying so; use `--firmware-image` with something you built, or drop `--pr`.

#### How the RAPI link differs

The image's entrypoint runs `socat` itself, so a containerised firmware takes RAPI over TCP
rather than a PTY, and the host bridges nothing:

| firmware | emulator | RAPI link                                                    |
| -------- | -------- | ------------------------------------------------------------ |
| local    | docker   | host `socat` PTY → published TCP port                        |
| local    | local    | emulator creates the PTY directly                            |
| local    | none     | host loopback PTY, nothing on the far end                    |
| docker   | docker   | private Docker network, `openevse_emulator_<i>:8023`         |
| docker   | local    | `host.docker.internal:8023+i`, emulator switched to TCP mode |
| docker   | none     | a TCP sink on `8023+i` that accepts and never replies        |

When both are containers they get a private network rather than talking through the host,
so the emulator's RAPI port stays published on loopback only.

Because the container brings its own filesystem and makes its own PTY, `--workdir` and
`--rapi-serial` mean nothing there — both are rejected rather than silently ignored. Note
also that state does not persist across runs the way it does for a local instance with its
per-instance working directory.

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

Assumes you have pasted the [opt-in block](#enable-it) into `.claude/settings.local.json`;
without it there is no sandbox to verify and these all just run normally.

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
