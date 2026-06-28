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

    display_draw_bitmap_scaled(20, 20, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, 1);

    display_draw_bitmap_scaled(50, 20, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, 2);

    display_draw_bitmap_scaled(90, 20, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, 4);

    display_draw_bitmap_scaled(160, 20, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, 8);

    display_draw_bitmap_scaled_bg(20, 100, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, 8, TFT_WHITE, TFT_RED);

    display_draw_bitmap_scaled_bg(120, 100, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, 4, TFT_BLUE, TFT_BLACK);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}