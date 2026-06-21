#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/spi_master.h"
#include "esp_heap_caps.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"

#define LCD_HOST SPI2_HOST

#define PIN_NUM_MOSI 23
#define PIN_NUM_MISO 19
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5
#define PIN_NUM_DC   21
#define PIN_NUM_RST  22

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

    uint8_t pixfmt = 0x55;   // RGB565
    esp_lcd_panel_io_tx_param(io_handle, 0x3A, &pixfmt, 1);

    ESP_ERROR_CHECK(
        esp_lcd_panel_swap_xy(
            panel_handle,
            true));

    ESP_ERROR_CHECK(
        esp_lcd_panel_mirror(
            panel_handle,
            true,
            false));

    ESP_ERROR_CHECK(
        esp_lcd_panel_disp_on_off(
            panel_handle,
            true));
}

static uint16_t rgb_to_panel(uint16_t color)
{
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5)  & 0x3F;
    uint16_t b = color & 0x1F;

    return (b << 11) | (r << 5) | g;
}

void display_fill(uint16_t color)
{
    color = rgb_to_panel(color);
#if 1
    for (int i = 0;i < LCD_H_RES * BUFFER_LINES;i++)
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
                (g << 5)  |
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