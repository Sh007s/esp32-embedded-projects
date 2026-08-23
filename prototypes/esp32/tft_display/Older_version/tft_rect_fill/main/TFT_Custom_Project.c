#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <unistd.h>
#include "display.h"

void app_main(void)
{
    display_init();
    display_fill(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1)
    {
        display_fill(TFT_BLACK);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(TFT_RED);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(TFT_GREEN);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(TFT_BLUE);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(TFT_YELLOW);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(TFT_CYAN);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(TFT_MAGENTA);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(0xFD20); // Orange
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(0x8410); // Gray
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(0x07FF); // Aqua
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(0xFFE0); // Yellow
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(0xF81F); // Pink/Magenta
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(0x07E0); // Lime Green
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(0x0010); // Navy
        vTaskDelay(pdMS_TO_TICKS(1000));

        // display_fill_rect(120, 0, 120, 80, TFT_GREEN);
        // vTaskDelay(pdMS_TO_TICKS(250));

        // display_fill_rect(0, 80, 120, 80, TFT_BLUE);
        // vTaskDelay(pdMS_TO_TICKS(250));

        // display_fill_rect(120, 80, 120, 80, TFT_YELLOW);
        // vTaskDelay(pdMS_TO_TICKS(50));

        // display_fill_rect(0, 160, 120, 80, TFT_CYAN);
        // vTaskDelay(pdMS_TO_TICKS(50));

        // display_fill_rect(120, 160, 120, 80, TFT_MAGENTA);
        // vTaskDelay(pdMS_TO_TICKS(50));

        // display_fill_rect(0, 240, 120, 80, TFT_WHITE);
        // vTaskDelay(pdMS_TO_TICKS(50));

        // display_fill_rect(120, 240, 120, 80, TFT_BLACK);
        // vTaskDelay(pdMS_TO_TICKS(50));
    }
}