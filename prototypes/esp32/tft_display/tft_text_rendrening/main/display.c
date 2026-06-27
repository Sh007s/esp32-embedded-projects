#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/spi_master.h"
#include "esp_heap_caps.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include <stdlib.h>
#include "font5x7.h"

#define LCD_HOST SPI2_HOST

#define PIN_NUM_MOSI 23
#define PIN_NUM_MISO 19
#define PIN_NUM_CLK 18
#define PIN_NUM_CS 5
#define PIN_NUM_DC 21
#define PIN_NUM_RST 22

#define LCD_H_RES 240
#define LCD_V_RES 320
#define BUFFER_LINES 20

static esp_lcd_panel_handle_t panel_handle = NULL;
static uint16_t *screen_buf = NULL;

void display_init(void)
{
    screen_buf = heap_caps_malloc(
        LCD_H_RES * BUFFER_LINES * sizeof(uint16_t),
        MALLOC_CAP_DMA);

    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_CLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz =
            LCD_H_RES * BUFFER_LINES * sizeof(uint16_t),
    };

    ESP_ERROR_CHECK(
        spi_bus_initialize(
            LCD_HOST,
            &buscfg,
            SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = 20000000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)LCD_HOST,
            &io_config,
            &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_ili9341(
            io_handle,
            &panel_config,
            &panel_handle));

    ESP_ERROR_CHECK(
        esp_lcd_panel_reset(panel_handle));

    ESP_ERROR_CHECK(
        esp_lcd_panel_init(panel_handle));

    uint8_t pixfmt = 0x55; // RGB565
    esp_lcd_panel_io_tx_param(io_handle, 0x3A, &pixfmt, 1);

    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));

    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    ESP_ERROR_CHECK(
        esp_lcd_panel_disp_on_off(
            panel_handle,
            true));
}

static uint16_t rgb_to_panel(uint16_t color)
{
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5) & 0x3F;
    uint16_t b = color & 0x1F;

    return (b << 11) | (r << 5) | g;
}

void display_fill(uint16_t color)
{
    color = rgb_to_panel(color);
#if 1
    for (int i = 0; i < LCD_H_RES * BUFFER_LINES; i++)
    {
        screen_buf[i] = color;
    }

    for (int y = 0;
         y < LCD_V_RES;
         y += BUFFER_LINES)
    {
        esp_lcd_panel_draw_bitmap(
            panel_handle,
            0,
            y,
            LCD_H_RES,
            y + BUFFER_LINES,
            screen_buf);
    }
#endif
    //  display_fill_rect(0, 0,LCD_H_RES, LCD_V_RES,color);
}

void display_gradient(void)
{
    for (int y = 0; y < LCD_V_RES; y++)
    {
        for (int x = 0; x < LCD_H_RES; x++)
        {
            uint8_t r = (x * 31) / LCD_H_RES;
            uint8_t g = (y * 63) / LCD_V_RES;
            uint8_t b = ((x + y) * 31) / (LCD_H_RES + LCD_V_RES);

            uint16_t color =
                (r << 11) |
                (g << 5) |
                b;

            screen_buf[x] = rgb_to_panel(color);
        }

        esp_lcd_panel_draw_bitmap(
            panel_handle,
            0,
            y,
            LCD_H_RES,
            y + 1,
            screen_buf);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
void display_fill_rect(
    int x,
    int y,
    int w,
    int h,
    uint16_t color)
{
    color = rgb_to_panel(color);

    for (int i = 0; i < w * BUFFER_LINES; i++)
    {
        screen_buf[i] = color;
    }

    for (int yy = y; yy < (y + h); yy += BUFFER_LINES)
    {
        int lines = BUFFER_LINES;

        if ((yy + lines) > (y + h))
        {
            lines = (y + h) - yy;
        }

        esp_lcd_panel_draw_bitmap(
            panel_handle,
            x,
            yy,
            x + w,
            yy + lines,
            screen_buf);
    }
}

void display_draw_pixel(int x, int y, uint16_t color)
{
    color = rgb_to_panel(color);

    uint16_t pixel = color;

    esp_lcd_panel_draw_bitmap(
        panel_handle,
        x,
        y,
        x + 1,
        y + 1,
        &pixel);
}

void display_draw_hline(int x, int y, int h, uint16_t color)
{
    display_fill_rect(x, y, h, 1, color);
}

void display_draw_vline(int x, int y, int v, uint16_t color)
{
    display_fill_rect(x, y, 1, v, color);
}
void display_draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;

    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;

    int err = dx + dy;

    while (1)
    {
        display_draw_pixel(x0, y0, color);

        if ((x0 == x1) && (y0 == y1))
        {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void display_draw_rect(int x, int y, int width, int height, uint16_t color)
{
    // Top
    display_draw_hline(x, y, width, color);

    // Bottom
    display_draw_hline(x, y + height - 1, width, color);

    // Left
    display_draw_vline(x, y, height, color);

    // Right
    display_draw_vline(x + width - 1, y, height, color);
}

void display_draw_circle(int xc, int yc, int r, uint16_t color)
{
    int x = 0;
    int y = r;
    int d = 3 - (2 * r);

    while (x <= y)
    {
        display_draw_pixel(xc + x, yc + y, color);
        display_draw_pixel(xc - x, yc + y, color);
        display_draw_pixel(xc + x, yc - y, color);
        display_draw_pixel(xc - x, yc - y, color);

        display_draw_pixel(xc + y, yc + x, color);
        display_draw_pixel(xc - y, yc + x, color);
        display_draw_pixel(xc + y, yc - x, color);
        display_draw_pixel(xc - y, yc - x, color);

        if (d < 0)
        {
            d = d + (4 * x) + 6;
        }
        else
        {
            d = d + (4 * (x - y)) + 10;
            y--;
        }

        x++;
    }
}

void display_fill_circle(int xc, int yc, int r, uint16_t color)
{
    int x = 0;
    int y = r;
    int d = 3 - (2 * r);

    while (y >= x)
    {
        // Upper and lower sections
        display_draw_hline(xc - x, yc - y, (2 * x) + 1, color);
        display_draw_hline(xc - x, yc + y, (2 * x) + 1, color);

        display_draw_hline(xc - y, yc - x, (2 * y) + 1, color);
        display_draw_hline(xc - y, yc + x, (2 * y) + 1, color);

        x++;

        if (d > 0)
        {
            y--;
            d = d + 4 * (x - y) + 10;
        }
        else
        {
            d = d + 4 * x + 6;
        }
    }
}
/*
void display_draw_char(int x, int y, char c, uint16_t color)
{
    const uint8_t *bitmap = NULL;

    switch (c)
    {
    case 'A':
        bitmap = font5x7[1];
        break;

    case 'B':
        bitmap = font5x7[2];
        break;

    case 'C':
        bitmap = font5x7[3];
        break;

    default:
        bitmap = font5x7[0];
        break;
    }

    for (int col = 0; col < 5; col++)
    {
        for (int row = 0; row < 7; row++)
        {
            if (bitmap[col] & (1 << (row)))
            {
                    // display_draw_pixel(x + (4 - col), y + row, color);
                display_draw_pixel(x + row, y + col, color);
            }
        }
    }
}
*/

void display_draw_char(int x, int y, char c, uint16_t color)
{
    /* Replace unsupported characters with SPACE */
    if ((unsigned char)c < 32 || (unsigned char)c > 126)
    {
        c = ' ';
    }

    /* Get bitmap from ASCII table */
    const uint8_t *bitmap = font5x7[(unsigned char)c - 32];

    for (int col = 0; col < 5; col++)
    {
        for (int row = 0; row < 7; row++)
        {
            if (bitmap[col] & (1 << row))
            {
                /* Your display orientation */
                display_draw_pixel(x + row, y + col, color);

                /*
                 * If you later change the display orientation,
                 * you may instead use:
                 *
                 * display_draw_pixel(x + col, y + row, color);
                 */
            }
        }
    }
}

void display_draw_string(int x, int y, const char *str, uint16_t color)
{
    while (*str)
    {
        display_draw_char(x, y, *str, color);
        y += 8;
        // x += 8;

        str++;
    }
}