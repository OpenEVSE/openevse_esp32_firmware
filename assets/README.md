# Firmware artwork

SVG sources rasterised into LVGL alpha masks by `scripts/gen_lvgl_mark.mjs`.

`mark-fault.svg` lives here rather than in the `gui-nightshift` submodule --
where the ordinary charge point mark comes from -- because it has no web UI
counterpart: nothing in the GUI shows a fault-marked charge point. If the web
UI ever grows one, move this to the GUI's assets and point the generator there,
so the two cannot drift.

Only the geometry survives rasterisation. The masks are 8-bit alpha and are
tinted at draw time from the runtime palette (shell on the theme accent, the
alert glyph on the error colour), so the colours in the file are documentation,
not output -- `currentColor` marks the shell exactly as it does in the GUI's
`mark.svg`, which is what the generator splits on.
