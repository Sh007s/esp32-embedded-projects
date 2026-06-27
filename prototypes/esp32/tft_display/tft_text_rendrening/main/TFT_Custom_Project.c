#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"

void app_main(void)
{
    display_init();

    display_fill(TFT_BLACK);

    /* Normal text */
    display_draw_string(20, 20, "display_draw_string()", TFT_WHITE);

    /* Background text */
    display_draw_string_bg(40, 20, "display_draw_string_bg()", TFT_WHITE, TFT_RED);

    /* Scaled text */
    display_draw_string_scaled(60, 20, "Scale 1", TFT_GREEN, 1);

    display_draw_string_scaled(80, 20, "Scale 2", TFT_YELLOW, 2);

    display_draw_string_scaled(100, 20, "Scale 3", TFT_CYAN, 3);

    /* Character width */
    uint16_t char_width = display_get_char_width();

    display_draw_string(130, 20, "Char Width:", TFT_WHITE);

    display_draw_char(130, 130, '0' + char_width, TFT_GREEN);

    /* Character height */
    uint16_t char_height = display_get_char_height();

    display_draw_string(150, 20, "Char Height:", TFT_WHITE);

    display_draw_char(150, 140, '0' + char_height, TFT_GREEN);

    /* String width */
    uint16_t string_width = display_get_string_width("HELLO");

    display_draw_string(180, 20, "HELLO Width:", TFT_WHITE);

    if (string_width >= 10)
    {
        display_draw_char(180, 135, '0' + (string_width / 10), TFT_GREEN);

        display_draw_char(180, 141, '0' + (string_width % 10), TFT_GREEN);
    }
    else
    {
        display_draw_char(180, 141, '0' + string_width, TFT_GREEN);
    }

    /* Center aligned */
    display_draw_string_center(120, 180, "CENTER", TFT_MAGENTA);

    /* Right aligned */
    display_draw_string_right(220, 275, "RIGHT", TFT_BLUE);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}