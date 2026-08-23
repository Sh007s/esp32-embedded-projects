#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

void display_init(void);
void display_fill(uint16_t color);
//static uint16_t rgb_to_panel(uint16_t color);
void display_gradient(void);
void display_fill_rect(int x,int y,int w,int h,uint16_t color);
void display_draw_pixel(int x, int y, uint16_t color);

#define TFT_BLACK   0x0000
#define TFT_BLUE    0x001F
#define TFT_RED     0xF800
#define TFT_PINK    0xF800
#define TFT_GREEN   0x07E0
#define TFT_CYAN    0x07FF
#define TFT_MAGENTA 0xF81F
#define TFT_YELLOW  0xFFE0
#define TFT_WHITE   0xFFFF

#endif