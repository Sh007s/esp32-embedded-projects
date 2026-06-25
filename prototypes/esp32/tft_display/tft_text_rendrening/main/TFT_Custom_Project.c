#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
void app_main()
{
    display_init();

    display_fill(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(1000));

    display_draw_char(100, 100, 'A', TFT_MAGENTA);
    display_draw_char(20, 20, 'A', TFT_BLUE);
    display_draw_char(20, 30, 'B', TFT_GREEN);
    display_draw_char(20, 40, 'C', TFT_RED);

    display_draw_pixel(10, 10, TFT_RED);
    display_draw_pixel(20, 10, TFT_GREEN);
    display_draw_pixel(10, 20, TFT_BLUE);

    display_draw_string(60, 100, "ABC", TFT_GREEN);
    display_draw_string(120, 100, "ACB", TFT_GREEN);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}