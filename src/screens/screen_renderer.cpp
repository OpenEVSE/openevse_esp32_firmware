#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_LCD)
#undef ENABLE_DEBUG
#endif

#ifdef ENABLE_SCREEN_LCD_TFT

#include "emonesp.h"
#include "screens/screen_renderer.h"
#include "lcd_common.h"
#include "embedded_files.h"
#include "web_server.h"
#include "lcd_static/lcd_gui_static_files.h"

extern PNG png;

// Global message buffer shared with renderers
char _msg[LCD_MAX_LINES][LCD_MAX_LEN + 1];
bool _msg_cleared = false;

#define MAX_IMAGE_WIDTH TFT_HEIGHT // Adjust for your images

struct image_render_state {
  TFT_eSPI *tft;
  int16_t xpos;
  int16_t ypos;
};

// Forward declaration for PNG decoder callback
static int png_draw(PNGDRAW *pDraw);

void render_image(const char *filename, int16_t x, int16_t y, TFT_eSPI &screen)
{
  StaticFile *file = NULL;
  if(embedded_get_file(filename, lcd_gui_static_files, ARRAY_LENGTH(lcd_gui_static_files), &file))
  {
    int16_t rc = png.openFLASH((uint8_t *)file->data, file->length, png_draw);
    if (rc == PNG_SUCCESS)
    {
      screen.startWrite();
      image_render_state state = {&screen, x, y};
      rc = png.decode(&state, 0);
      screen.endWrite();
    }
  }
}

void render_text_box(const char *text, int16_t x, int16_t y, int16_t text_x, int16_t width,
                     const GFXfont *font, uint16_t text_colour, uint16_t back_colour,
                     bool fill_back, uint8_t d, uint8_t size, TFT_eSPI &screen)
{
  TFT_eSprite sprite(&screen);

  sprite.setFreeFont(font);
  sprite.setTextSize(size);
  sprite.setTextDatum(d);
  sprite.setTextColor(text_colour, back_colour);

  int16_t height = sprite.fontHeight();
  uint16_t *pixels = (uint16_t *)sprite.createSprite(width, height);
  if(nullptr == pixels)
  {
    DBUGF("Failed to create sprite for text box %d x %d", width, height);
    return;
  }

  sprite.fillSprite(back_colour);
  sprite.drawString(text, text_x, height);

  screen.startWrite();
  screen.pushImage(x, y - height, width, height, pixels);
  screen.endWrite();

  sprite.deleteSprite();
}

void render_centered_text_box(const char *text, int16_t x, int16_t y, int16_t width,
                              const GFXfont *font, uint16_t text_colour, uint16_t back_colour,
                              bool fill_back, uint8_t size, TFT_eSPI &screen)
{
  render_text_box(text, x, y, (width / 2), width, font, text_colour, back_colour, fill_back, BC_DATUM, size, screen);
}

void render_right_text_box(const char *text, int16_t x, int16_t y, int16_t width,
                           const GFXfont *font, uint16_t text_colour, uint16_t back_colour,
                           bool fill_back, uint8_t size, TFT_eSPI &screen)
{
  render_text_box(text, x, y, width, width, font, text_colour, back_colour, fill_back, BR_DATUM, size, screen);
}

void render_left_text_box(const char *text, int16_t x, int16_t y, int16_t width,
                          const GFXfont *font, uint16_t text_colour, uint16_t back_colour,
                          bool fill_back, uint8_t size, TFT_eSPI &screen)
{
  render_text_box(text, x, y, 0, width, font, text_colour, back_colour, fill_back, BL_DATUM, size, screen);
}

void render_data_box(const char *title, const char *text, int16_t x, int16_t y,
                     int16_t width, int16_t height, bool full_update, TFT_eSPI &screen)
{
  if(full_update)
  {
    screen.fillSmoothRoundRect(x, y, width, height, 6, TFT_OPENEVSE_INFO_BACK, TFT_WHITE);
  }
  render_centered_text_box(title, x, y+24, width, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_INFO_BACK, !full_update, 1, screen);
  render_centered_text_box(text, x, y+(height-4), width, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_INFO_BACK, !full_update, 1, screen);
}

void render_info_box(const char *title, const char *text, int16_t x, int16_t y,
                     int16_t width, int16_t height, bool full_update, TFT_eSPI &screen)
{
  if(full_update)
  {
    screen.fillSmoothRoundRect(x, y, width, height, 6, TFT_OPENEVSE_INFO_BACK, TFT_WHITE);
    render_centered_text_box(title, x, y+24, width, &FreeSans9pt7b, TFT_OPENEVSE_GREEN, TFT_OPENEVSE_INFO_BACK, false, 1, screen);
  }
  render_centered_text_box(text, x, y+(height-4), width, &FreeSans9pt7b, TFT_WHITE, TFT_OPENEVSE_INFO_BACK, !full_update, 1, screen);
}

void get_scaled_number_value(double value, int precision, const char *unit, char *buffer, size_t size)
{
  static const char *mod[] = {
    "",
    "k",
    "M",
    "G",
    "T",
    "P"
  };

  int index = 0;
  while (value > 1000 && index < ARRAY_ITEMS(mod))
  {
    value /= 1000;
    index++;
  }

  snprintf(buffer, size, "%.*f %s%s", precision, value, mod[index], unit);
}

void set_message_line(int x, int y, const char *msg, bool clear)
{
  DBUGF("LCD: %d %d %s, clear=%s", x, y, msg, clear ? "true" : "false");

  if(y < 0 || y >= LCD_MAX_LINES) {
    return;
  }

  if(clear) {
    clear_message_line(y);
  }

  if(x < 0 || x >= LCD_MAX_LEN) {
    return;
  }

  strncpy(_msg[y] + x, msg, LCD_MAX_LEN - x);
  _msg[y][LCD_MAX_LEN] = '\0';
  _msg_cleared = false;

  DBUGF("LCD: %s (%p)", _msg[y], _msg[y]);
}

void clear_message_line(int line)
{
  if(line < 0 || line >= LCD_MAX_LINES) {
    return;
  }

  memset(_msg[line], ' ', LCD_MAX_LEN);
  _msg[line][LCD_MAX_LEN] = '\0';
}

String get_message_line(int line)
{
  if(line < 0 || line >= LCD_MAX_LINES) {
    return "";
  }

  // trim leading and trailing spaces
  int len = LCD_MAX_LEN;
  while(len > 0 && _msg[line][len - 1] == ' ') {
    len--;
  }
  char *start = _msg[line];
  while(len > 0 && *start == ' ') {
    start++;
    len--;
  }

  DBUGF("get_message_line: %d, '%s' (%p) -> '%.*s'", line, _msg[line], _msg[line], len, start);

  return String(start, len);
}

static int png_draw(PNGDRAW *pDraw)
{
  image_render_state *state = (image_render_state *)pDraw->pUser;
  uint16_t lineBuffer[MAX_IMAGE_WIDTH];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  state->tft->pushImage(state->xpos, state->ypos + pDraw->y, pDraw->iWidth, 1, lineBuffer);
  return 1;
}

#endif // ENABLE_SCREEN_LCD_TFT
