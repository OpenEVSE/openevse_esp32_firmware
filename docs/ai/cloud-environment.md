# Claude Code on the web — cloud environment setup

How to configure a Claude Code on the web (claude.ai/code) **cloud environment** for this
repo so `pio run` / `pio test` and the rest of the [validation gate](../../AGENTS.md#validation-gate--run-after-any-change)
work out of the box in a fresh session, instead of failing on the first build.

This is a different mechanism from [`docs/ai/sandbox.md`](sandbox.md): that page covers the
**local, opt-in Bash sandbox** for the Claude Code CLI running on your own machine. This page
covers the **cloud environment's own network policy**, configured in the environment's settings
on claude.ai (Edit cloud environment → Network access), which governs every session that runs
in that environment. See also the general docs at
[code.claude.com/docs/en/claude-code-on-the-web](https://code.claude.com/docs/en/claude-code-on-the-web).

## All domains, to paste in one go

Everything below, for pasting into Network access → Custom → Allowed domains in one shot
instead of adding domains section by section. Skip `cdn.socket.io` if you don't care about
watching the emulator's own dashboard live (see that section for why it's separate).

```
*.platformio.org
ghcr.io
pkg-containers.githubusercontent.com
cdn.socket.io
```

Also leave **"Also include default list of common package managers"** checked — it's a
separate checkbox, not a domain to paste, and covers `github.com`, `registry.npmjs.org`,
`pypi.org`, and `files.pythonhosted.org` (see below). The rest of this page explains what each
domain above is for and what breaks without it.

## Required: Network access → Custom → Allowed domains

```
*.platformio.org
```

**Not** `*.platform.io` — that is a different, unrelated domain and silently fails to match
anything PlatformIO actually talks to. The hosts a build needs are subdomains of
`platformio.org`: `api.registry.platformio.org` and `api.registry.nm1.platformio.org` (package
metadata), `collector.platformio.org` (telemetry), and the CDN host(s) that serve the actual
package downloads. A wildcard on the whole domain is simpler and more future-proof than
enumerating each one, since PlatformIO can add or rename subdomains without notice.

Also leave **"Also include default list of common package managers"** checked — this repo's
`gui-nightshift` (npm) and `divert_sim` (pip) dependencies need `registry.npmjs.org`,
`pypi.org`, and `files.pythonhosted.org`, which that default list covers, on top of
`github.com` and friends for git submodules and PlatformIO's own git-hosted dependencies.

None of this applies if the environment uses "Full access" instead of "Custom".

## What happens without it

`pio run` / `pio test` need to download platform packages (compiler toolchains, framework
sources) from PlatformIO's registry on first use in every fresh container — `~/.platformio` is
not pre-seeded. Without `*.platformio.org` allowed, every build or test fails with an
`HTTPClientError`, even though `pip install platformio` itself succeeds (that only needs
PyPI, a separate domain, which the "common package managers" default already allows).

## Optional: emulator / integration tests (Docker image pulls)

Only needed for `scripts/openevse_test.sh emulator`/`integration`/`launch` (the default
`--emulator docker`, or `--firmware docker`/`--pr N`) — `unit`, `divert`, `gui`, `native`, and
the [validation gate](../../AGENTS.md#validation-gate--run-after-any-change) above don't touch
Docker at all. These pull prebuilt images from GitHub Container Registry:

- `ghcr.io/jeremypoulter/openevse_emulator` — the emulator
- `ghcr.io/openevse/openevse-wifi-native` — a PR's or tag's firmware build, for `--firmware
  docker`/`--pr N`

Add to Allowed domains:

```
ghcr.io
pkg-containers.githubusercontent.com
```

`ghcr.io` resolves the manifest and auth, but the actual layer bytes are served from
`pkg-containers.githubusercontent.com` — the Azure blob-storage host GHCR redirects downloads
to. Confirmed by testing: a pull gets past the `ghcr.io` manifest fine and then fails with
`403 Forbidden` from `pkg-containers.githubusercontent.com` if that second host isn't allowed —
the same failure mode as the PlatformIO registry above, just one layer further into the
request.

The container **Docker daemon** itself is a separate prerequisite from network access, and the
checked-in SessionStart hook below doesn't start it (it only bootstraps the native/PlatformIO/
npm/pip path). If `docker info` reports no daemon running, start one before using `emulator`/
`integration`/`launch`: `dockerd &`, then give it a few seconds to come up. The daemon and any
`launch`ed containers do not survive a session ending — a fresh session needs `dockerd &` again,
and `socat` (used to bridge RAPI to the emulator's container) may need reinstalling too if the
base image doesn't carry it.

### Viewing the emulator's own web dashboard

The emulator's JSON API (`/api/status` etc., what `pytest tests/integration/` and RAPI actually
use) needs nothing beyond the two domains above. Its human-facing dashboard page is a separate
concern: the page loads its Socket.IO client from `cdn.socket.io`, and that `<script>` tag is
unconditional — if it's blocked, the whole inline script that follows throws on the now-undefined
`io()` call and never runs, so every field on the page (state, current, power) stays frozen at
its initial "Ready" values instead of live-updating, even though the backend is working
correctly. Confirmed by testing: `curl` to `/api/status` shows real state changes while the
rendered page does not, and the browser console shows a `ReferenceError`. Only relevant if you
want to look at that dashboard yourself (e.g. taking a screenshot) rather than drive the emulator
through its API — add to Allowed domains:

```
cdn.socket.io
```

## The checked-in SessionStart hook

[`.claude/hooks/session-start.sh`](../../.claude/hooks/session-start.sh) (registered in
[`.claude/settings.json`](../../.claude/settings.json)) bootstraps a fresh session: installs
the Avahi/OpenSSL dev headers native builds need, pins PlatformIO to the version this repo's
CI uses, runs `git submodule update --init --recursive` (`gui-nightshift` and `migrator` are
**not** initialized by a plain checkout), and installs `gui-nightshift`'s npm deps and
`divert_sim`'s pip deps. It only runs when `$CLAUDE_CODE_REMOTE=true`, so it is a no-op
locally. It travels with the repo/branch — no per-environment configuration needed — but it
cannot grant network access; that is only the "Allowed domains" setting above.

## The environment's own "Setup script" field

This is a separate mechanism from the repo's checked-in hook above, configured per-environment
rather than per-repo. This repo's bootstrap lives entirely in the checked-in
`.claude/hooks/session-start.sh`, so there is no need to duplicate it in the environment's
Setup script field — leave that field empty (or whatever the environment needs for reasons
unrelated to this repo).
