#ifndef __SCREEN_RENDERER_H
#define __SCREEN_RENDERER_H

#include <TFT_eSPI.h>
#include <PNGdec.h>

// The TFT screen is portrait natively, so we need to rotate it
#define TFT_SCREEN_WIDTH  TFT_HEIGHT
#define TFT_SCREEN_HEIGHT TFT_WIDTH

// Shared rendering functions
extern void render_image(const char *filename, int16_t x, int16_t y, TFT_eSPI &screen);

extern void render_text_box(const char *text, int16_t x, int16_t y, int16_t text_x, int16_t width,
                     const GFXfont *font, uint16_t text_colour, uint16_t back_colour,
                     bool fill_back, uint8_t d, uint8_t size, TFT_eSPI &screen);

extern void render_centered_text_box(const char *text, int16_t x, int16_t y, int16_t width,
                              const GFXfont *font, uint16_t text_colour, uint16_t back_colour,
                              bool fill_back, uint8_t size, TFT_eSPI &screen);

extern void render_right_text_box(const char *text, int16_t x, int16_t y, int16_t width,
                           const GFXfont *font, uint16_t text_colour, uint16_t back_colour,
                           bool fill_back, uint8_t size, TFT_eSPI &screen);

extern void render_left_text_box(const char *text, int16_t x, int16_t y, int16_t width,
                          const GFXfont *font, uint16_t text_colour, uint16_t back_colour,
                          bool fill_back, uint8_t size, TFT_eSPI &screen);

extern void render_data_box(const char *title, const char *text, int16_t x, int16_t y,
                     int16_t width, int16_t height, bool full_update, TFT_eSPI &screen);

extern void render_info_box(const char *title, const char *text, int16_t x, int16_t y,
                     int16_t width, int16_t height, bool full_update, TFT_eSPI &screen);

extern void get_scaled_number_value(double value, int precision, const char *unit,
                             char *buffer, size_t size);

extern String get_message_line(int line);

#endif // __SCREEN_RENDERER_H
