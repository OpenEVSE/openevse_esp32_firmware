#ifndef __LCD_H
#define __LCD_H

#include <Arduino.h>

#define LCD_DISPLAY_NOW -1

void lcd_display(const __FlashStringHelper *msg, int x, int y, int time, bool clear);
void lcd_display(String &msg, int x, int y, int time, bool clear);
void lcd_display(const char *msg, int x, int y, int time, bool clear);
void lcd_loop();

#endif // __LCD_H
