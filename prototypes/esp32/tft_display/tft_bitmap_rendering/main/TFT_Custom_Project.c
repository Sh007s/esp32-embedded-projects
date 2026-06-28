#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "bitmap_data.h"

void app_main(void)
{
    display_init();

    display_fill(TFT_BLACK);
    generate_bitmap16();
    generate_bitmap32();
    generate_bitmap64();

    // display_draw_bitmap(20, 20, 8, 8, test_bitmap);

    // display_draw_bitmap(20, 40, BITMAP16_WIDTH, BITMAP16_HEIGHT, bitmap16x16);

    // display_draw_bitmap(20, 70, BITMAP32_WIDTH, BITMAP32_HEIGHT, bitmap32x32);

    // display_draw_bitmap(20, 120, BITMAP64_WIDTH, BITMAP64_HEIGHT, bitmap64x64);

    display_draw_bitmap_bg(20, 20, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, TFT_WHITE, TFT_BLACK);

    display_draw_bitmap_bg(20, 40, BITMAP16_WIDTH, BITMAP16_HEIGHT, bitmap16x16, TFT_WHITE, TFT_RED);

    display_draw_bitmap_bg(20, 70, BITMAP32_WIDTH, BITMAP32_HEIGHT, bitmap32x32, TFT_BLUE, TFT_BLACK);

    display_draw_bitmap_bg(20, 120, BITMAP64_WIDTH, BITMAP64_HEIGHT, bitmap64x64, TFT_CYAN, TFT_MAGENTA);
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}