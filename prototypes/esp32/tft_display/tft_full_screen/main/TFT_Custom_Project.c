#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"

void app_main(void)
{
    display_init();

    while (1)
    {
        display_fill(TFT_BLACK);
        vTaskDelay(pdMS_TO_TICKS(2000));
        display_fill(TFT_RED);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_GREEN);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_BLUE);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_BLACK);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_CYAN);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_MAGENTA);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_YELLOW);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(0x0001); // Very dark blue
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}