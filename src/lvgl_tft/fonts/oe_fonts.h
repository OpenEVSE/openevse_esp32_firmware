// Subset Montserrat faces for the two biggest text sizes on the LVGL screens.
//
// LVGL's built-in montserrat_48 and _36 carry all of printable ASCII plus ~60
// FontAwesome symbols, uncompressed: ~48 KB and ~28 KB of flash. Between them
// eight labels use those sizes, drawing about twenty distinct characters, so
// almost all of it was dead weight. These subsets cost ~3.5 KB and ~1.8 KB.
//
// !! EACH CARRIES ONLY THE GLYPHS LISTED BELOW. !! Anything else renders as
// nothing at all -- LVGL does not fall back to another face. Adding a character
// to a string drawn in one of these means adding it to
// scripts/gen_lvgl_subset_fonts.sh and regenerating.
//
// The smaller sizes stay on the stock built-ins: they carry the LV_SYMBOL_*
// glyphs the status chips and advisory lines draw, and they are cheap anyway.
#ifndef __OE_FONTS_H
#define __OE_FONTS_H

#ifdef ENABLE_SCREEN_LVGL_TFT

#include <lvgl.h>

// Glyphs: space - . 0-9 E O S V e n p
// Drawn by: charge_screen big_value (the kW readout, which can carry a minus
// sign -- $GG answers -1 mA without AMMETER), boot_screen title ("OpenEVSE").
// Nothing else may use this face without regenerating it.
LV_FONT_DECLARE(lv_font_oe_display_48);

// Glyphs: space - . 0-9 : A W h k
// Drawn by: the stat tile values on charge_screen and standby_screen --
// "%02u:%02u:%02u", "%.2f kWh", "%.0f Wh", "%.1f A", "--".
LV_FONT_DECLARE(lv_font_oe_tile_36);

#endif // ENABLE_SCREEN_LVGL_TFT
#endif // __OE_FONTS_H
