// Rasterise the OpenEVSE charge point marks into LVGL 8-bit alpha masks.
//
// Each mark has two colour roles: the enclosure and cord follow the theme
// accent, the inner glyph does not. A single baked RGB bitmap would freeze both
// and break the panel's dark/light themes, so each mark is split into two
// LV_IMG_CF_ALPHA_8BIT masks. LVGL draws an A8 image entirely in the style's
// img_recolor, so each mask is tinted at draw time and both follow
// ns_set_theme() for free.
//
// One byte per pixel, so a 64x64 mask is 4 KB of flash and nothing at runtime --
// lv_img reads straight from the const array.
//
//   node scripts/gen_lvgl_mark.mjs
//
// The ordinary mark is read from gui-nightshift/src/assets/mark.svg so the panel
// and the web UI cannot drift apart silently. The fault mark has no web UI
// counterpart and lives in assets/ -- see assets/README.md.
//
// Only the alpha channel survives, so the colours in the sources are
// documentation. What matters is the split: anything painted currentColor is
// the shell, everything else is the glyph.

import sharp from '../gui-nightshift/node_modules/sharp/dist/index.mjs'
import { readFileSync, writeFileSync } from 'node:fs'

const MARKS = [
  {
    src: new URL('../gui-nightshift/src/assets/mark.svg', import.meta.url),
    out: new URL('../src/lvgl_tft/mark_img.c', import.meta.url),
    header: 'mark_img.h',
    // 64 px is the floor for this artwork, not a layout preference: the cord is
    // a 4.5 stroke in a 100-unit viewBox, so it lands at 2.9 device pixels at
    // that size. Go much smaller and it drops under two pixels and greys out on
    // a panel with no subpixel help. Do NOT scale at runtime -- LVGL's image
    // transform would eat it. 80 is what the boot and standby screens are drawn
    // around.
    size: 80,
    label: 'gui-nightshift/src/assets/mark.svg',
    shellName: 'mark_shell_img',
    glyphName: 'mark_bolt_img',
    note: ['the enclosure and cord are tinted with',
           'the theme accent and the bolt with the brand violet, so the mark follows'],
  },
  {
    src: new URL('../assets/mark-fault.svg', import.meta.url),
    out: new URL('../src/lvgl_tft/mark_fault_img.c', import.meta.url),
    header: 'mark_fault_img.h',
    // The fault screen's title row, which is why this one is smaller than the
    // boot/standby mark. Still at the 64 px floor above -- the cord and plug
    // are the first things to disappear below it.
    size: 64,
    label: 'assets/mark-fault.svg',
    shellName: 'mark_fault_shell_img',
    glyphName: 'mark_fault_alert_img',
    note: ['the enclosure, cord and plug are tinted',
           'with the theme accent and the alert glyph with the error colour, so it follows'],
  },
]

function shapes(svg) {
  // circle is in the list for the fault mark's dot; rect and path cover the
  // rest. Anything new in the artwork has to be added here or it silently
  // vanishes from the mask, so the count is asserted below.
  const found = svg.match(/<(?:rect|path|circle)\b[^>]*\/>/g)
  if(!found || found.length < 2) {
    throw new Error(`expected at least 2 shapes, found ${found ? found.length : 0}`)
  }
  return found
}

// currentColor has no meaning outside a document; the fill/stroke colour is
// irrelevant anyway because only the alpha channel survives.
const solid = (s) => s.replace(/currentColor/g, '#000')

const wrap = (body, size) =>
  `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="${size}" height="${size}">${body}</svg>`

async function alphaOf(body, size) {
  const { data, info } = await sharp(Buffer.from(wrap(body, size)))
    .resize(size, size)
    .ensureAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true })

  const out = Buffer.alloc(size * size)
  for(let i = 0; i < size * size; i++) {
    out[i] = data[i * info.channels + (info.channels - 1)]
  }
  return out
}

function emit(name, bytes, size) {
  let s = `static const uint8_t ${name}_map[] = {\n`
  for(let i = 0; i < bytes.length; i += 16) {
    s += '  ' + [...bytes.subarray(i, i + 16)].map((b) => `0x${b.toString(16).padStart(2, '0')}`).join(', ') + ',\n'
  }
  s += '};\n\n'
  s += `const lv_img_dsc_t ${name} = {\n`
  s += `  .header = { .cf = LV_IMG_CF_ALPHA_8BIT, .always_zero = 0, .reserved = 0,\n`
  s += `              .w = ${size}, .h = ${size} },\n`
  s += `  .data_size = sizeof(${name}_map),\n`
  s += `  .data = ${name}_map,\n`
  s += `};\n\n`
  return s
}

for(const m of MARKS) {
  const svg = readFileSync(m.src, 'utf8')
  const all = shapes(svg)

  // Split by colour role, not by position: the mark has gained shapes before (a
  // second cord, the plug head) and will again.
  const shell = all.filter((s) => s.includes('currentColor'))
  const glyph = all.filter((s) => !s.includes('currentColor'))
  if(shell.length === 0 || glyph.length === 0) {
    throw new Error(`${m.src.pathname} has ${shell.length} shell and ${glyph.length} glyph shapes; expected both`)
  }

  const shellA = await alphaOf(shell.map(solid).join(''), m.size)
  const glyphA = await alphaOf(glyph.map(solid).join(''), m.size)

  let out = `// GENERATED by scripts/gen_lvgl_mark.mjs -- do not edit by hand.\n`
  out += `// Source: ${m.label}, rasterised at ${m.size}x${m.size}.\n`
  out += `//\n`
  out += `// Two 8-bit alpha masks, not one bitmap: ${m.note[0]}\n`
  out += `// ${m.note[1]}\n`
  out += `// ns_set_theme(). Callers MUST set both img_recolor and img_recolor_opa --\n`
  out += `// LVGL only reads the colour when the opa is above zero and otherwise draws\n`
  out += `// the mask in black (lv_obj_draw.c, lv_obj_init_draw_img_dsc).\n`
  out += `#ifdef ENABLE_SCREEN_LVGL_TFT\n\n`
  out += `#include <lvgl.h>\n\n`
  out += `#include "${m.header}"\n\n`
  out += emit(m.shellName, shellA, m.size)
  out += emit(m.glyphName, glyphA, m.size)
  out += `#endif // ENABLE_SCREEN_LVGL_TFT\n`

  writeFileSync(m.out, out)
  console.log(`wrote ${m.out.pathname}: 2 x ${m.size}x${m.size} A8 (${m.size * m.size} B each)`)
}
