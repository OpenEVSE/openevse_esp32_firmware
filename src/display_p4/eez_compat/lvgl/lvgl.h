/* Compat shim for the EEZ Studio export.
 *
 * EEZ generates `#include <lvgl/lvgl.h>`, but the firmware's lvgl lib puts its
 * own directory on the include path (so lvgl is reached as <lvgl.h>). Adding
 * `-I src/display_p4/eez_compat` lets the `lvgl/lvgl.h` form resolve here and
 * forward to the real header — no need to hand-edit the generated export.
 */
#include <lvgl.h>
