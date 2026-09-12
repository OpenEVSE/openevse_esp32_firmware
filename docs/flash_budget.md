# Flash budget — `openevse_wifi_v1` and the other 4MB boards

24 of the 26 PlatformIO environments build against the 4MB `min_spiffs.csv`
layout, whose app partition is **1,966,080 bytes**. That image has been
running within a few KB of the limit, and in early 2026 it crossed it: CI
failed `openevse_wifi_v1` with

```
Error: The program size (1968045 bytes) is greater than maximum allowed (1966080 bytes)
```

Only `openevse_wifi_tft_v1` (+`_dev`) and `openevse_wifi_v1_16mb` have 16MB
partitions and room to spare.

> **Update:** the numbers below were measured against master at the time this
> was written. Master has since dropped the legacy TFT_eSPI renderer
> (`chore: drop the legacy TFT_eSPI renderer and the Elecrow envs`, #1226),
> which freed far more flash than this document's own trims did — a
> CI-accurate build of current master (per the reproduction steps below)
> measures **1,791,161 bytes (91.1%)**, not the 1,968,045 (over budget) this
> page originally reported. The overflow that motivated this work is no
> longer an active emergency. The PNG recompression itself is unaffected by
> any of that — it is still a free, pixel-identical 11KB saving — so it's
> kept below on its own merits, not as a crisis fix.

## Reproducing a CI-accurate build locally

**A local `pio run` does not measure what CI measures.** `scripts/extra_script.py`
regenerates `src/web_static/` from `gui-nightshift/dist` whenever the submodule
has `node_modules` *and* a built `dist`. A developer with the GUI checked out —
especially on a different branch — therefore embeds different assets than CI,
which has no `node_modules` and falls back to the committed headers.

That difference is easily thousands of bytes, and it is why a local build can
pass while CI fails on the same commit.

To measure what CI measures, point `GUI_NAME` at a directory that does not
exist. `extra_script.py` then skips regeneration and the build uses the tracked
headers, exactly as CI does:

```sh
GUI_NAME=__use_tracked_web_static__ pio run -e openevse_wifi_v1
```

(It prints `Warning: GUI files not found` — that is the intended path here.)

## Where the space goes

The embedded web UI is the single largest block: **286,808 bytes, 14.6%** of
the app partition, across 18 assets in `src/web_static/`. The four language
bundles are ~57KB of that, the main JS bundle ~102KB, vendor ~55KB.

## Trims measured on `openevse_wifi_v1`

Baseline 1,968,045 (over by 1,965) *at the time these were measured* — see
the update note above; current master is well clear of the limit regardless
of these trims. Each measured independently:

| Change | Saving | Cost |
|---|---|---|
| **PNG recompression** (applied) | **11,136** | none — pixel-identical |
| `-UENABLE_DEBUG` | 10,812 | loses all serial debug logging |
| `-UMG_ENABLE_EXTRA_ERRORS_DESC -DMG_ENABLE_EXTRA_ERRORS_DESC=0` | 1,500 | loses descriptive Mongoose error text |
| `-UMO_TRAFFIC_OUT` | 252 | loses OCPP traffic logging |
| `-DMO_DBG_LEVEL=MO_DL_WARN` | 0 | — (no effect; already dead-stripped) |
| `-Os` explicitly | 0 | — (already the default) |

Two things worth noting from that table:

- `openevse_wifi_v1` is a **production** environment that nonetheless pulls in
  `${common.debug_flags}` — `ENABLE_DEBUG`, `ENABLE_FULL_RAPI`, `ENABLE_OTA`.
  Debug logging alone is ~10.8KB. It goes to `Serial1` (`DEBUG_PORT`), so this
  looks deliberate rather than accidental, and it has been left alone: it is a
  real diagnostic capability, and the PNG work made it unnecessary to touch.
  It remains the largest single lever if more room is ever needed.
- `-ggdb` is also in that set but costs **nothing** in flash — debug info lives
  in the ELF, not the `.bin`.

## What was applied: lossless PNG recompression

PNG stores pixels as a zlib stream. The encoders that produced these icons did
not use maximum compression, so re-deflating the existing image data at level 9
— trying each zlib strategy and keeping the smallest — recovers 14–36% per file
with **no change to a single pixel**.

`optimise_png()` in `scripts/extra_script.py` concatenates the `IDAT` chunks,
inflates them, re-deflates that byte stream, and rebuilds the file. The
*filtered* scanline data handed to zlib is passed through untouched, so the
transform cannot alter an image; every other chunk (`IHDR`, `PLTE`, `tRNS`,
`pHYs`, …) is copied verbatim. The function verifies its own output round-trips
and refuses to grow a file.

| Asset | Before | After | Saved |
|---|---|---|---|
| `pwa_512x512.png` | 15,917 | 11,222 | 4,695 (29.5%) |
| `pwa_maskable_512x512.png` | 14,998 | 9,683 | 5,315 (35.4%) |
| `pwa_192x192.png` | 4,383 | 3,790 | 593 (13.5%) |
| `apple_touch_icon.png` | 3,965 | 3,432 | 533 (13.4%) |
| **web_static total** | **39,263** | **28,127** | **11,136** |
| `src/lcd_static/` (15 TFT icons) | 41,117 | 40,173 | 944 (2.3%) |

The TFT icons were already well compressed; the PWA icons were not.

Verification: every icon was decoded before and after and compared as a raw
pixel raster — all 19 are byte-identical. The regeneration also re-emitted each
header from its *existing* bytes first and required that to reproduce the file
exactly, so the generated format is provably unchanged.

Result on `openevse_wifi_v1` at the time: **1,957,345 bytes, 99.6%, 8,735
free.** (Current master, with the legacy TFT renderer gone, sits at
**1,791,161 bytes, 91.1%** regardless of this trim — see the update note
above.)

Because the optimisation lives in the header generator, any future GUI rebuild
keeps it automatically — there is nothing to remember.

## If more room is needed later

In rough order of value per unit of pain:

1. **Split `ENABLE_DEBUG` out of `openevse_wifi_v1`** (~10.8KB) — the biggest
   single lever, at the cost of field diagnostics on `Serial1`. An
   `openevse_wifi_v1_dev` env could keep debug for developers, as other boards
   already do.
2. **Brotli instead of gzip** for the JS/CSS assets (~245KB of the image).
   Typically 15–20% better than gzip, so plausibly 30KB+ — but it needs a
   `Content-Encoding: br` path in the web server and a build-pipeline change.
3. **Palette-reduce the PWA icons** (`ct6` RGBA → `ct3`) — still lossless for
   flat-colour artwork, but it needs a real optimiser (oxipng/pngquant) in the
   toolchain rather than stdlib zlib.
4. `MG_ENABLE_EXTRA_ERRORS_DESC=0` (~1.5KB) and `-UMO_TRAFFIC_OUT` (~0.25KB).

## Watch out for

`src/web_static/` is **generated but tracked**. Anyone who builds with the GUI
submodule present will see those files change, and committing them silently
swaps the embedded UI for whatever they had checked out. Check `git status`
before committing after a build.
