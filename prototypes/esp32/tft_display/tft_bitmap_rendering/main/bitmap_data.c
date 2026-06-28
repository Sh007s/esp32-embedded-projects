#include "bitmap_data.h"
#include "display.h"
#include <stddef.h>

/*---------------------------------------------------------
 * 8x8 Test Bitmap
 *--------------------------------------------------------*/
const uint16_t test_bitmap[TEST_BITMAP_WIDTH * TEST_BITMAP_HEIGHT] =
{
    TFT_RED,   TFT_RED,   TFT_RED,   TFT_RED,   TFT_RED,   TFT_RED,   TFT_RED,   TFT_RED,
    TFT_RED,   TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_RED,
    TFT_RED,   TFT_WHITE, TFT_BLUE,  TFT_BLUE,  TFT_BLUE,  TFT_BLUE,  TFT_WHITE, TFT_RED,
    TFT_RED,   TFT_WHITE, TFT_BLUE,  TFT_GREEN, TFT_GREEN, TFT_BLUE,  TFT_WHITE, TFT_RED,
    TFT_RED,   TFT_WHITE, TFT_BLUE,  TFT_GREEN, TFT_GREEN, TFT_BLUE,  TFT_WHITE, TFT_RED,
    TFT_RED,   TFT_WHITE, TFT_BLUE,  TFT_BLUE,  TFT_BLUE,  TFT_BLUE,  TFT_WHITE, TFT_RED,
    TFT_RED,   TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_RED,
    TFT_RED,   TFT_RED,   TFT_RED,   TFT_RED,   TFT_RED,   TFT_RED,   TFT_RED,   TFT_RED
};

/*---------------------------------------------------------
 * Generated Bitmap Buffers
 *--------------------------------------------------------*/
uint16_t bitmap16x16[BITMAP16_WIDTH * BITMAP16_HEIGHT];
uint16_t bitmap32x32[BITMAP32_WIDTH * BITMAP32_HEIGHT];
uint16_t bitmap64x64[BITMAP64_WIDTH * BITMAP64_HEIGHT];


void display_draw_bitmap(int x,int y,int width,int height, const uint16_t *bitmap)
{
    if (bitmap == NULL)
    {
        return;
    }

    for (int row = 0; row < height; row++)
    {
        for (int col = 0; col < width; col++)
        {
            display_draw_pixel(
                x + col,
                y + row,
                bitmap[row * width + col]);
        }
    }
}

/*---------------------------------------------------------
 * Generate 16x16 Checkerboard
 *--------------------------------------------------------*/
void generate_bitmap16(void)
{
    for (int y = 0; y < BITMAP16_HEIGHT; y++)
    {
        for (int x = 0; x < BITMAP16_WIDTH; x++)
        {
            if ((x + y) % 2 == 0)
            {
                bitmap16x16[y * BITMAP16_WIDTH + x] = TFT_RED;
            }
            else
            {
                bitmap16x16[y * BITMAP16_WIDTH + x] = TFT_WHITE;
            }
        }
    }
}

/*---------------------------------------------------------
 * Generate 32x32 Checkerboard
 *--------------------------------------------------------*/
void generate_bitmap32(void)
{
    for (int y = 0; y < BITMAP32_HEIGHT; y++)
    {
        for (int x = 0; x < BITMAP32_WIDTH; x++)
        {
            if (((x / 4) + (y / 4)) % 2 == 0)
            {
                bitmap32x32[y * BITMAP32_WIDTH + x] = TFT_GREEN;
            }
            else
            {
                bitmap32x32[y * BITMAP32_WIDTH + x] = TFT_BLUE;
            }
        }
    }
}

/*---------------------------------------------------------
 * Generate 64x64 Checkerboard
 *--------------------------------------------------------*/
void generate_bitmap64(void)
{
    for (int y = 0; y < BITMAP64_HEIGHT; y++)
    {
        for (int x = 0; x < BITMAP64_WIDTH; x++)
        {
            if (((x / 8) + (y / 8)) % 2 == 0)
            {
                bitmap64x64[y * BITMAP64_WIDTH + x] = TFT_YELLOW;
            }
            else
            {
                bitmap64x64[y * BITMAP64_WIDTH + x] = TFT_CYAN;
            }
        }
    }
}

void display_draw_bitmap_bg(int x, int y, int width, int height, const uint16_t *bitmap, uint16_t transparent_color, uint16_t bg_color)
{
    if (bitmap == NULL)
    {
        return;
    }

    for (int row = 0; row < height; row++)
    {
        for (int col = 0; col < width; col++)
        {
            uint16_t color = bitmap[row * width + col];


            if (color == transparent_color)
            {
                display_draw_pixel(
                    x + col,
                    y + row,
                    bg_color);
            }
            else
            {
                display_draw_pixel(
                    x + col,
                    y + row,
                    color);
            }
        }
    }
}

void display_draw_bitmap_scaled(int x,int y,int width, int height, const uint16_t *bitmap, uint8_t scale)
{
    if (bitmap == NULL)
    {
        return;
    }

    if (scale == 0)
    {
        return;
    }

    for (int row = 0; row < height; row++)
    {
        for (int col = 0; col < width; col++)
        {
            uint16_t color = bitmap[row * width + col];

            for (int sy = 0; sy < scale; sy++)
            {
                for (int sx = 0; sx < scale; sx++)
                {
                    display_draw_pixel(
                        x + (col * scale) + sx,
                        y + (row * scale) + sy,
                        color);
                }
            }
        }
    }
}

void display_draw_bitmap_scaled_bg(int x,int y,  int width,int height,  const uint16_t *bitmap,uint8_t scale,uint16_t transparent_color,uint16_t bg_color)
{
    if (bitmap == NULL || scale == 0)
    {
        return;
    }

    for (int row = 0; row < height; row++)
    {
        for (int col = 0; col < width; col++)
        {
            uint16_t color = bitmap[row * width + col];

            if (color == transparent_color)
            {
                color = bg_color;
            }

            for (int sy = 0; sy < scale; sy++)
            {
                for (int sx = 0; sx < scale; sx++)
                {
                    display_draw_pixel(
                        x + (col * scale) + sx,
                        y + (row * scale) + sy,
                        color);
                }
            }
        }
    }
}