#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#if 1
void app_main(void)
{
    display_init();

    display_fill(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(500));

    display_draw_vline(0, 0, 320, TFT_RED);     // Left edge
    display_draw_vline(239, 0, 320, TFT_GREEN); // Right edge

    display_draw_hline(0, 0, 240, TFT_BLUE);    // Top edge
    display_draw_hline(0, 319, 240, TFT_WHITE); // Bottom edge

    // // Diagonal
    display_draw_line(0, 0, 239, 319, TFT_BLUE);

    // // Reverse Diagonal
    display_draw_line(239, 0, 0, 319, TFT_YELLOW);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#else
void app_main(void)
{
    display_init();

    // Clear screen
    display_fill(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Corner pixels
    display_draw_pixel(0, 0, TFT_WHITE);
    display_draw_pixel(239, 0, TFT_RED);
    display_draw_pixel(0, 319, TFT_GREEN);
    display_draw_pixel(239, 319, TFT_BLUE);

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Center Cross
    for (int x = 0; x < 240; x++)
    {
        display_draw_pixel(x, 160, TFT_RED);
    }

    for (int y = 0; y < 320; y++)
    {
        display_draw_pixel(120, y, TFT_GREEN);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Rectangle
    for (int x = 50; x <= 150; x++)
    {
        display_draw_pixel(x, 50, TFT_YELLOW);  // Top
        display_draw_pixel(x, 150, TFT_YELLOW); // Bottom
    }

    for (int y = 50; y <= 150; y++)
    {
        display_draw_pixel(50, y, TFT_YELLOW);  // Left
        display_draw_pixel(150, y, TFT_YELLOW); // Right
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Diagonal Line
    for (int i = 0; i < 100; i++)
    {
        display_draw_pixel(50 + i, 50 + i, TFT_CYAN);
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif
