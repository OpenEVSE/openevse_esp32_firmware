#ifndef __LCD_COMMON_H
#define __LCD_COMMON_H

#include <TFT_eSPI.h>
//#include <Fonts/FreeSans9pt7b.h>
//#include <Fonts/FreeSans24pt7b.h>

// LCD colour definitions
#define TFT_OPENEVSE_BACK       0x2413
#define TFT_OPENEVSE_GREEN      0x3E92
#define TFT_OPENEVSE_TEXT       0x1BD1
#define TFT_OPENEVSE_INFO_BACK  0x23d1

// Message definitions
#define LCD_MAX_LEN 16
#define LCD_MAX_LINES 2

#define LCD_CLEAR_LINE    (1 << 0)
#define LCD_DISPLAY_NOW   (1 << 1)

#ifndef LCD_BACKLIGHT_PIN
#define LCD_BACKLIGHT_PIN TFT_BL
#endif

// Helper macro
#ifndef ARRAY_ITEMS
#define ARRAY_ITEMS(a) (sizeof(a) / sizeof(a[0]))
#endif

// Message line buffer - shared across screens
extern char _msg[LCD_MAX_LINES][LCD_MAX_LEN + 1];

#endif // __LCD_COMMON_H
