#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "bitmap_data.h"
#include "image_data.h"
#define VISION_PIXEL_WIDTH 8
#define VISION_PIXEL_HEIGHT 8

const uint16_t vision_pixel[VISION_PIXEL_WIDTH * VISION_PIXEL_HEIGHT] =
    {
        /* Row 0 */
        TFT_RED, TFT_RED, TFT_RED, TFT_RED, TFT_RED, TFT_RED, TFT_RED, TFT_RED,

        /* Row 1 */
        TFT_RED, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_BLACK, TFT_RED,

        /* Row 2 */
        TFT_RED, TFT_WHITE, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_RED,

        /* Row 3 */
        TFT_RED, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_WHITE, TFT_BLACK, TFT_BLACK, TFT_RED,

        /* Row 4 */
        TFT_RED, TFT_WHITE, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_RED,

        /* Row 5 */
        TFT_RED, TFT_WHITE, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_RED,

        /* Row 6 */
        TFT_RED, TFT_WHITE, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_BLACK, TFT_RED,

        /* Row 7 */
        TFT_RED, TFT_RED, TFT_RED, TFT_RED, TFT_RED, TFT_RED, TFT_RED, TFT_RED};

void app_main(void)
{
    display_init();

    display_fill(TFT_BLACK);

    generate_bitmap16();
    generate_bitmap32();
    generate_bitmap64();

    /******************************************************
     * PART 1 - RGB565 Image Rendering
     ******************************************************/

    // Original RGB565 Image
    display_draw_rgb565_image(10, 10, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, test_image);

    // Scaled RGB565 Image
    display_draw_rgb565_image_scaled(40, 10, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, 8, test_image);

    // Fast RGB565 Image
    display_draw_rgb565_image_fast(120, 10, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, test_image);

    // Push Image
    display_push_image(180, 10, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, test_image);

    /******************************************************
     * PART 2 - Icon Rendering
     ******************************************************/

    display_draw_icon(10, 100, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap);

    display_draw_icon_transparent(40, 100, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, TFT_BLACK);

    /******************************************************
     * PART 3 - Sprite Rendering
     ******************************************************/

    display_draw_sprite(80, 100, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap);

    display_draw_sprite_transparent(110, 100, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, TFT_BLACK);

    /******************************************************
     * PART 4 - Bitmap Operations
     ******************************************************/

    display_draw_bitmap_rotate(150, 100, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, 90);

    display_draw_bitmap_transparent(180, 100, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, TFT_BLACK);

    display_draw_bitmap_flip_horizontal(10, 160, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap);

    display_draw_bitmap_flip_vertical(40, 160, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap);

    /******************************************************
     * PART 5 - Scaled Bitmap Operations
     ******************************************************/

    display_draw_bitmap_scaled(10, 190, VISION_PIXEL_WIDTH, VISION_PIXEL_HEIGHT, vision_pixel, 4);

    display_draw_bitmap_flip_horizontal_scaled(60, 190, VISION_PIXEL_WIDTH, VISION_PIXEL_HEIGHT, vision_pixel, 4);

    display_draw_bitmap_flip_vertical_scaled(110, 190, VISION_PIXEL_WIDTH, VISION_PIXEL_HEIGHT, vision_pixel, 4);

    display_draw_bitmap_rotate_scaled(160, 190, VISION_PIXEL_WIDTH, VISION_PIXEL_HEIGHT, vision_pixel, 90, 4);
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}