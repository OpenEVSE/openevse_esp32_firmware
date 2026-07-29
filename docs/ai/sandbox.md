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
- **Prompts on network-mutating actions** (`permissions.ask`) — `git push` and the
  device OTA flash (`curl ... /update`) still surface a confirmation even in auto-allow.
- **docker escape hatch** (`sandbox.excludedCommands`) — `docker` is incompatible with
  the sandbox and is used by the integration tests, so it runs outside the sandbox via
  the normal permission flow.

## Caveats

- The proxy allows by hostname and does **not** inspect TLS by default, so a broad
  domain like `github.com` is still a possible exfiltration path (domain fronting). Keep
  the allowlist as narrow as the build tolerates.
- The sandbox confines Bash only. MCP servers and hooks run unconfined on the host.
- For unattended / `--dangerously-skip-permissions` runs, this host-level sandbox is not
  enough — use a dev container with a default-deny egress firewall (the Anthropic
  reference container) or a VM. That is a deliberate follow-up, not set up here.

## Verify

```bash
pio test -e native_test          # host unit tests run sandboxed
pio run -e native_openevse       # native firmware binary builds sandboxed
cd gui-nightshift && npm run build && npm test && cd ..
cd divert_sim && pytest -v && cd ..
curl https://example.com         # should be BLOCKED/prompted — proves the allowlist
cat ~/.ssh/id_* 2>&1             # should be denied inside a sandboxed command
```
