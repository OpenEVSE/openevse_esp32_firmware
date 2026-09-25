#!/usr/bin/env bash
#
# Regenerate the subset LVGL fonts in src/lvgl_tft/fonts/.
#
# LVGL's built-in Montserrat faces carry the whole of printable ASCII plus ~60
# FontAwesome symbols, uncompressed, which at the big sizes is most of what the
# renderer costs in flash: montserrat_48 alone is ~48 KB and montserrat_36 ~28 KB.
# Two labels use 48 and six use 36, between them rendering about twenty distinct
# characters, so the rest is dead weight.
#
# Subsetting those two down to the glyphs actually reachable takes ~76 KB to
# ~5 KB. The smaller sizes are left as the stock built-ins: they carry the
# LV_SYMBOL_* glyphs (WIFI, CHARGE, WARNING, BULLET) that the chips and the
# status lines draw, and they are cheap anyway.
#
# !! IF YOU ADD A CHARACTER TO A STRING DRAWN IN ONE OF THESE FACES, ADD IT HERE
# !! AND REGENERATE, or LVGL will render nothing for it. The glyph sets below
# !! name every call site so the reachable set can be checked by reading them.
#
# Deliberately NOT --compressed: LVGL decompresses per glyph at draw time, and
# this panel is already bus-bound with the CPU doing an 18bpp conversion per
# pixel. Flash is the cheaper thing to spend here.
#
# Needs: npm i -g lv_font_conv   (tested with 1.5.2)
set -euo pipefail

OUT="$(dirname "$0")/../src/lvgl_tft/fonts"

# Montserrat-Medium.ttf ships inside the LVGL library, so there is no font
# binary in this repo to keep in sync. Point TTF= at it if the glob misses.
TTF="${TTF:-$(ls -d "$(dirname "$0")"/../.pio/libdeps/*/lvgl/scripts/built_in_font/Montserrat-Medium.ttf 2>/dev/null | head -1)}"
if [ ! -f "$TTF" ]; then
  echo "Montserrat-Medium.ttf not found; build any LVGL env first, or set TTF=" >&2
  exit 1
fi

# 48 px -- charge_screen.cpp big_value (the kW readout: digits and '.') and
# boot_screen.cpp title (the wordmark "OpenEVSE").
lv_font_conv --no-compress --no-prefilter --bpp 4 --size 48 --font "$TTF" \
  --symbols ' .0123456789EOSVenp' \
  --format lvgl -o "$OUT/lv_font_oe_display_48.c" --force-fast-kern-format

# 36 px -- the stat tile values on charge_screen.cpp and standby_screen.cpp:
#   "%02u:%02u:%02u"  "%.2f kWh"  "%.0f Wh"  "%.1f A"  "--"
lv_font_conv --no-compress --no-prefilter --bpp 4 --size 36 --font "$TTF" \
  --symbols ' -.0123456789:AWhk' \
  --format lvgl -o "$OUT/lv_font_oe_tile_36.c" --force-fast-kern-format

# Pin line_height to the built-in face's value.
#
# lv_font_conv derives line_height from the tallest and deepest glyph it was
# given, so a subset computes a SMALLER one than the full face (48 px: 43 vs 52,
# 36 px: 37 vs 40) purely because the dropped glyphs included the tall ASCII and
# the FontAwesome symbols. line_height feeds label layout, not rasterising, so
# letting it shrink would silently move every label drawn in these faces --
# a visible change to shipped screens in exchange for nothing. Restore the
# built-in numbers and the swap is invisible. There is no lv_font_conv flag for
# either, hence the rewrite.
#
# base_line is the descent depth and goes the same way: the 36 px set is all
# digits, punctuation and A/W/h/k, none of which drop below the baseline, so it
# computes 0 against the full face's 7. (The 48 px set already matches at 9 --
# "OpenEVSE" contributes a 'p'.)
sed -i 's#\.line_height = [0-9]*,#.line_height = 52,#' "$OUT/lv_font_oe_display_48.c"
sed -i 's#\.line_height = [0-9]*,#.line_height = 40,#; s#\.base_line = [0-9]*,#.base_line = 7,#' \
  "$OUT/lv_font_oe_tile_36.c"

# Gate the whole file on ENABLE_SCREEN_LVGL_TFT, ahead of its include.
#
# PlatformIO compiles everything under src/ for every env, including the ones
# with no display and so no LVGL in lib_deps -- native_openevse is the one CI
# catches. lv_font_conv's output opens with `#include "lvgl/lvgl.h"` (or
# "lvgl.h" under LV_LVGL_H_INCLUDE_SIMPLE), which those envs cannot resolve.
# Same guard, in the same place, as src/lvgl_tft/mark_img.c.
for f in "$OUT"/lv_font_oe_display_48.c "$OUT"/lv_font_oe_tile_36.c; do
  { echo "// Guard added by scripts/gen_lvgl_subset_fonts.sh -- see that script."
    echo "#ifdef ENABLE_SCREEN_LVGL_TFT"
    echo
    cat "$f"
    echo
    echo "#endif // ENABLE_SCREEN_LVGL_TFT"
  } > "$f.tmp" && mv "$f.tmp" "$f"
done

# lv_font_conv records its own argv in a header comment, including the absolute
# path it was handed for the TTF -- which differs per machine and per PIO env.
# Rewrite it so regenerating on another box produces a byte-identical file.
for f in "$OUT"/lv_font_oe_display_48.c "$OUT"/lv_font_oe_tile_36.c; do
  sed -i 's#--font [^ ]*Montserrat-Medium.ttf#--font Montserrat-Medium.ttf#; s#-o [^ ]*/src/lvgl_tft/fonts/#-o src/lvgl_tft/fonts/#' "$f"
done

echo "regenerated:"
ls -l "$OUT"
