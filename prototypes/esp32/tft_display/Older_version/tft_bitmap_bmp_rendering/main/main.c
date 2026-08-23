#if 1
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "bmp.h"
#include "esp_spiffs.h"
#include "esp_log.h"

void app_main(void)
{
    display_init();
    display_fill(TFT_BLACK);

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        printf("SPIFFS Mount Failed: %s\n", esp_err_to_name(ret));
        return;
    }

    printf("SPIFFS Mounted Successfully\n");

    // display_draw_bmp(0, 0, "/spiffs/t3.bmp");
    // display_draw_string(25, 100, "RED", TFT_WHITE);

    while (1)
    {
        display_draw_bmp(0, 0, "/spiffs/t3.bmp");
        display_draw_string(25, 100, "RED", TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(TFT_RED);
        display_draw_string(25, 100, "RED", TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(TFT_GREEN);
        display_draw_string(25, 100, "GREEN", TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(TFT_BLUE);
        display_draw_string(25, 100, "BLUE", TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(1000));

        display_fill(TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // display_draw_bmp(0, 0, "/spiffs/green.bmp");
        // display_draw_string(25, 100, "GREEN", TFT_WHITE);
        // vTaskDelay(pdMS_TO_TICKS(2000));

        // display_draw_bmp(0, 0, "/spiffs/blue.bmp");
        // display_draw_string(25, 100, "BLUE", TFT_WHITE);
        // vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
#else

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"

#define TEST_W 100
#define TEST_H 100

static uint16_t test_img[TEST_W * TEST_H];

void app_main(void)
{
    display_init();
    display_fill(TFT_BLACK);

    for (int y = 0; y < TEST_H; y++)
    {
        for (int x = 0; x < TEST_W; x++)
        {
            if (x < 50 && y < 50)
            {
                test_img[y * TEST_W + x] = TFT_RED;
            }
            else if (x >= 50 && y < 50)
            {
                test_img[y * TEST_W + x] = TFT_GREEN;
            }
            else if (x < 50 && y >= 50)
            {
                test_img[y * TEST_W + x] = TFT_BLUE;
            }
            else
            {
                test_img[y * TEST_W + x] = TFT_WHITE;
            }
        }
    }

    display_push_image(70, 110, TEST_W, TEST_H, test_img);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#endif