#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"

void app_main(void)
{
    display_init();

    display_fill(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // // Normal text
    // display_draw_string(20, 20, "display_draw_char()", TFT_WHITE);

    // display_draw_string(40, 40, "HELLO TFT", TFT_RED);

    // display_draw_string(60, 60, "Shankar S", TFT_GREEN);

    // display_draw_string(80, 80, "1234567890", TFT_CYAN);

    // display_draw_string(90, 80, "shal", TFT_CYAN);

    // // Character with background
    // display_draw_string(100, 110, "display_draw_char_bg()", TFT_YELLOW);

    // display_draw_char_bg(30, 5, 'A', TFT_WHITE, TFT_RED);
    // display_draw_char_bg(40, 5, 'B', TFT_WHITE, TFT_GREEN);
    // display_draw_char_bg(50, 5, 'C', TFT_WHITE, TFT_BLUE);
    // display_draw_char_bg(60, 5, 'D', TFT_WHITE, TFT_YELLOW);
    // display_draw_char_bg(70, 5, 'E', TFT_WHITE, TFT_MAGENTA);

    // display_draw_string_bg(20, 20, "HELLO TFT", TFT_WHITE, TFT_RED);

    // display_draw_string_bg(40, 20, "ESP32", TFT_YELLOW, TFT_BLUE);

    // display_draw_string_bg(60, 20, "Shankar S", TFT_RED, TFT_YELLOW);

    display_draw_string_scaled(20, 20, "HELLO", TFT_WHITE, 1);

    display_draw_string_scaled(40, 55, "HELLO", TFT_RED, 2);

    display_draw_string_scaled(80, 100, "HELLO", TFT_GREEN, 3);

    display_draw_string_scaled(110, 200, "HELLO", TFT_CYAN, 4);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}