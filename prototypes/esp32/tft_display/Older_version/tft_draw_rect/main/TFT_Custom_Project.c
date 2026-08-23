#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"

void app_main(void)
{
    display_init();

    display_fill(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(2000));

    display_draw_rect(20, 20, 100, 50, TFT_RED);

    display_draw_rect(50, 100, 80, 80, TFT_GREEN);

    display_fill_rect(150, 50, 60, 60, TFT_BLUE);
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}