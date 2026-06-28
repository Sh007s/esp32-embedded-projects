#ifndef BITMAP_DATA_H
#define BITMAP_DATA_H

#include <stdint.h>
#include "display.h"

/* Bitmap Sizes */
#define TEST_BITMAP_WIDTH 8
#define TEST_BITMAP_HEIGHT 8

#define BITMAP16_WIDTH 16
#define BITMAP16_HEIGHT 16

#define BITMAP32_WIDTH 32
#define BITMAP32_HEIGHT 32

#define BITMAP64_WIDTH 64
#define BITMAP64_HEIGHT 64

/* Test Bitmap */
extern const uint16_t test_bitmap[TEST_BITMAP_WIDTH * TEST_BITMAP_HEIGHT];

/* Test Pattern Buffers */
extern uint16_t bitmap16x16[BITMAP16_WIDTH * BITMAP16_HEIGHT];
extern uint16_t bitmap32x32[BITMAP32_WIDTH * BITMAP32_HEIGHT];
extern uint16_t bitmap64x64[BITMAP64_WIDTH * BITMAP64_HEIGHT];

/* Pattern Generation Functions */
void generate_bitmap16(void);
void generate_bitmap32(void);
void generate_bitmap64(void);

void display_draw_bitmap(int x, int y, int width, int height, const uint16_t *bitmap);
void display_draw_bitmap_bg(int x, int y, int width, int height, const uint16_t *bitmap, uint16_t transparent_color, uint16_t bg_color);

#endif