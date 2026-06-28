#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "bitmap_data.h"

void app_main(void)
{
    display_init();

    display_fill(TFT_BLACK);

    // /* Normal text */
    // display_draw_string(20, 20, "display_draw_string()", TFT_WHITE);

    // /* Background text */
    // display_draw_string_bg(40, 20, "display_draw_string_bg()", TFT_WHITE, TFT_RED);

    // /* Scaled text */
    // display_draw_string_scaled(60, 20, "Scale 1", TFT_GREEN, 1);

    // display_draw_string_scaled(80, 20, "Scale 2", TFT_YELLOW, 2);

    // display_draw_string_scaled(100, 20, "Scale 3", TFT_CYAN, 3);

    // /* Character width */
    // uint16_t char_width = display_get_char_width();

    // display_draw_string(130, 20, "Char Width:", TFT_WHITE);

    // display_draw_char(130, 130, '0' + char_width, TFT_GREEN);

    // /* Character height */
    // uint16_t char_height = display_get_char_height();

    // display_draw_string(150, 20, "Char Height:", TFT_WHITE);

    // display_draw_char(150, 140, '0' + char_height, TFT_GREEN);

    // /* String width */
    // uint16_t string_width = display_get_string_width("HELLO");

    // display_draw_string(180, 20, "HELLO Width:", TFT_WHITE);

    // if (string_width >= 10)
    // {
    //     display_draw_char(180, 135, '0' + (string_width / 10), TFT_GREEN);

    //     display_draw_char(180, 141, '0' + (string_width % 10), TFT_GREEN);
    // }
    // else
    // {
    //     display_draw_char(180, 141, '0' + string_width, TFT_GREEN);
    // }

    // /* Center aligned */
    // display_draw_string_center(120, 180, "CENTER", TFT_MAGENTA);

    // /* Right aligned */
    // display_draw_string_right(220, 275, "RIGHT", TFT_BLUE);

    // display_printf(20, 20, TFT_WHITE, "HELLO TFT");

    // display_printf(40, 20, TFT_RED, "Speed : %d km/h", 80);

    // display_printf(60, 20, TFT_GREEN, "Temp : %.1f C", 27.5);

    // display_printf(80, 20, TFT_CYAN, "Battery : %d%%", 95);

    // display_printf(100, 20, TFT_YELLOW, "Version : %s", "V1.0");

    //    display_draw_string(120, 20, "Bitmap Test", TFT_WHITE);

    // /* Draw Bitmap */

    generate_bitmap16();
    generate_bitmap32();
    generate_bitmap64();

    display_draw_bitmap(20, 20, 8, 8, test_bitmap);

    display_draw_bitmap(20, 40, BITMAP16_WIDTH, BITMAP16_HEIGHT, bitmap16x16);

    display_draw_bitmap(20, 70, BITMAP32_WIDTH, BITMAP32_HEIGHT, bitmap32x32);

    display_draw_bitmap(20, 120, BITMAP64_WIDTH, BITMAP64_HEIGHT, bitmap64x64);

    // display_draw_rect(100, 20, 8, 8, TFT_RED);
    // display_draw_rect(100, 40, 16, 16, TFT_RED);
    // display_draw_rect(100, 70, 32, 32, TFT_RED);
    // display_draw_rect(100, 120, 64, 64, TFT_RED);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}