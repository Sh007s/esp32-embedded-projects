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

    // /******************************************************
    //  * PART 1
    //  ******************************************************/

    // /* RGB565 Image */
    // display_draw_rgb565_image(10, 10, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, test_image);

    // /* Scaled RGB565 Image */
    // display_draw_rgb565_image_scaled(40, 10, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, 8, test_image);

    // /* Fast RGB565 Image */
    // display_draw_rgb565_image_fast(140, 10, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, test_image);

    // /* Push Image */
    // display_push_image(170, 10, TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, test_image);

    // /******************************************************
    //  * PART 2
    //  ******************************************************/

    // /* Draw Icon */
    // display_draw_icon(10, 120, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap);

    // /* Transparent Icon */
    // display_draw_icon_transparent(40, 120, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, TFT_BLACK);

    // /******************************************************
    //  * PART 3
    //  ******************************************************/

    // /* Draw Sprite */
    // display_draw_sprite(80, 120, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap);

    // /* Transparent Sprite */
    // display_draw_sprite_transparent(110, 120, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, TFT_BLACK);

    // /******************************************************
    //  * PART 4
    //  ******************************************************/

    // /* Bitmap Rotation */
    // display_draw_bitmap_rotate(150, 120, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, 90);

    // /* Bitmap Transparent */
    // display_draw_bitmap_transparent(180, 120, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap, TFT_BLACK);

    // /* Flip Horizontal */
    // display_draw_bitmap_flip_horizontal(10, 180, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap);

    // /* Flip Vertical */
    // display_draw_bitmap_flip_vertical(40, 180, TEST_BITMAP_WIDTH, TEST_BITMAP_HEIGHT, test_bitmap);

    // /******************************************************
    //  * PART 5
    //  ******************************************************/

    // display_set_window(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // /******************************************************
    //  * PART 6
    //  ******************************************************/

    // /* These currently return ESP_ERR_NOT_SUPPORTED */

    // display_draw_bmp(0, 0, "/spiffs/test.bmp");

    // display_draw_jpeg(0, 0, "/spiffs/test.jpg");

    // display_draw_png(0, 0, "/spiffs/test.png");

    // display_draw_bitmap(
    //     20, 20,
    //     VISION_PIXEL_WIDTH,
    //     VISION_PIXEL_HEIGHT,
    //     vision_pixel);

    // display_draw_bitmap_flip_horizontal(
    //     60, 20,
    //     VISION_PIXEL_WIDTH,
    //     VISION_PIXEL_HEIGHT,
    //     vision_pixel);

    // display_draw_bitmap_flip_vertical(
    //     100, 20,
    //     VISION_PIXEL_WIDTH,
    //     VISION_PIXEL_HEIGHT,
    //     vision_pixel);

    // display_draw_bitmap_rotate(
    //     140, 20,
    //     VISION_PIXEL_WIDTH,
    //     VISION_PIXEL_HEIGHT,
    //     vision_pixel,
    //     90);

    // display_draw_bitmap_rotate(
    //     180, 20,
    //     VISION_PIXEL_WIDTH,
    //     VISION_PIXEL_HEIGHT,
    //     vision_pixel,
    //     180);

    // display_draw_bitmap_rotate(
    //     20, 60,
    //     VISION_PIXEL_WIDTH,
    //     VISION_PIXEL_HEIGHT,
    //     vision_pixel,
    //     270);

    display_draw_bitmap_scaled(
        10, 10,
        VISION_PIXEL_WIDTH,
        VISION_PIXEL_HEIGHT,
        vision_pixel,
        8);

    display_draw_bitmap_flip_horizontal_scaled(
        90, 10,
        VISION_PIXEL_WIDTH,
        VISION_PIXEL_HEIGHT,
        vision_pixel,
        8);

    display_draw_bitmap_flip_vertical_scaled(
        170, 10,
        VISION_PIXEL_WIDTH,
        VISION_PIXEL_HEIGHT,
        vision_pixel,
        8);

    display_draw_bitmap_rotate_scaled(
        10, 100,
        VISION_PIXEL_WIDTH,
        VISION_PIXEL_HEIGHT,
        vision_pixel,
        90,
        8);

    display_draw_bitmap_rotate_scaled(
        90, 100,
        VISION_PIXEL_WIDTH,
        VISION_PIXEL_HEIGHT,
        vision_pixel,
        180,
        8);

    display_draw_bitmap_rotate_scaled(
        170, 100,
        VISION_PIXEL_WIDTH,
        VISION_PIXEL_HEIGHT,
        vision_pixel,
        270,
        8);

    // display_draw_bitmap_scaled(
    //     20,
    //     20,
    //     VISION_PIXEL_WIDTH,
    //     VISION_PIXEL_HEIGHT,
    //     vision_pixel,
    //     10);

    // display_draw_bitmap_flip_horizontal_scaled(
    //     140,
    //     20,
    //     VISION_PIXEL_WIDTH,
    //     VISION_PIXEL_HEIGHT,
    //     vision_pixel,
    //     10);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}