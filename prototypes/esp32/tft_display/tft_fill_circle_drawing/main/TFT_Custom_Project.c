#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#if 0
void app_main(void)
{
    display_init();

    display_fill(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(2000));

    display_draw_circle(120, 160, 80, TFT_RED);

    display_draw_circle(120, 160, 50, TFT_GREEN);

    display_draw_circle(120, 160, 20, TFT_BLUE);
    display_draw_circle(120, 160, 100, TFT_YELLOW);
    display_draw_circle(120, 160, 119, TFT_MAGENTA);
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#else   

void app_main(void)
{
    display_init();

    display_fill(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(2000));
    display_fill_circle(40, 80, 20, TFT_BLUE);
    display_fill_circle(80, 120, 50, TFT_GREEN);
    display_fill_circle(240, 160, 80, TFT_RED);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#endif