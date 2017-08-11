#ifndef __LCD_H
#define __LCD_H

#include <Arduino.h>

#define LCD_CLEAR_LINE    (1 << 0)
#define LCD_DISPLAY_NOW   (1 << 1)

void lcd_display(const __FlashStringHelper *msg, int x, int y, int time, uint32_t flags);
void lcd_display(String &msg, int x, int y, int time, uint32_t flags);
void lcd_display(const char *msg, int x, int y, int time, uint32_t flags);
void lcd_loop();

#endif // __LCD_H
