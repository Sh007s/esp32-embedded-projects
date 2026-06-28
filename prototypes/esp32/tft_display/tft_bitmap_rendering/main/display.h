#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdarg.h>

#define TFT_BLACK 0x0000
#define TFT_BLUE 0x001F
#define TFT_RED 0xF800
#define TFT_PINK 0xF800
#define TFT_GREEN 0x07E0
#define TFT_CYAN 0x07FF
#define TFT_MAGENTA 0xF81F
#define TFT_YELLOW 0xFFE0
#define TFT_WHITE 0xFFFF

#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_SPACING 1

void display_init(void);
void display_fill(uint16_t color);
// static uint16_t rgb_to_panel(uint16_t color);
void display_gradient(void);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_draw_pixel(int x, int y, uint16_t color);
void display_draw_hline(int x, int y, int h, uint16_t color);
void display_draw_vline(int x, int y, int v, uint16_t color);
void display_draw_line(int x0, int y0, int x1, int y1, uint16_t color);
void display_draw_rect(int x, int y, int width, int height, uint16_t color);
void display_draw_circle(int xc, int yc, int r, uint16_t color);
void display_fill_circle(int xc, int yc, int r, uint16_t color);
void display_draw_char(int x, int y, char c, uint16_t color);
void display_draw_string(int x, int y, const char *str, uint16_t color);
void display_draw_char_bg(int x, int y, char c, uint16_t text_color, uint16_t bg_color);
void display_draw_string_bg(int x, int y, const char *str, uint16_t text_color, uint16_t bg_color);
void display_draw_char_scaled(int x, int y, char c, uint16_t color, uint8_t scale);
void display_draw_string_scaled(int x, int y, const char *str, uint16_t color, uint8_t scale);
uint16_t display_get_char_width(void);
uint16_t display_get_char_height(void);
uint16_t display_get_string_width(const char *str);
void display_draw_string_center(int x, int y, const char *str, uint16_t color);
void display_draw_string_right(int x, int y, const char *str, uint16_t color);
void display_printf(int x, int y, uint16_t color, const char *fmt, ...);
void display_draw_bitmap(int x, int y, int width, int height, const uint16_t *bitmap);

#endif