#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"

void app_main(void)
{
    display_init();
    display_fill(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1)
    {

        display_fill_rect(0, 0, 120, 80, TFT_RED);
        vTaskDelay(pdMS_TO_TICKS(50));

        display_fill_rect(120, 0, 120, 80, TFT_GREEN);
        vTaskDelay(pdMS_TO_TICKS(50));

        display_fill_rect(0, 80, 120, 80, TFT_BLUE);
        vTaskDelay(pdMS_TO_TICKS(50));

        display_fill_rect(120, 80, 120, 80, TFT_YELLOW);
        vTaskDelay(pdMS_TO_TICKS(50));

        display_fill_rect(0, 160, 120, 80, TFT_CYAN);
        vTaskDelay(pdMS_TO_TICKS(50));

        display_fill_rect(120, 160, 120, 80, TFT_MAGENTA);
        vTaskDelay(pdMS_TO_TICKS(50));

        display_fill_rect(0, 240, 120, 80, TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(50));

        display_fill_rect(120, 240, 120, 80, TFT_BLACK);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}