#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"

void app_main(void)
{
    display_init();

    display_fill(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(1000));

    display_draw_string(20, 20, "ABC", TFT_WHITE);
    display_draw_string(30, 40, "HELLO TFT", TFT_RED);
    display_draw_string(40, 60, "Shankar", TFT_GREEN);
    display_draw_string(50, 80, "1234567890", TFT_BLUE);

    int x = 80;
    int y = 0;

    for (int c = 32; c <= 126; c++)
    {
        display_draw_char(x, y, (char)c, TFT_YELLOW);

        printf("ASCII %d : %c\n", c, (char)c);

        y += 6+;                 // Next character

        if (y > 290)            // Next line
        {
            y = 0;
            x += 8;
        }
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}