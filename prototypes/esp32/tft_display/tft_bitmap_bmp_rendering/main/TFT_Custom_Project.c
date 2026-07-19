#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "bmp.h"
#include "esp_spiffs.h"
#include "esp_log.h"

void app_main(void)
{
    display_init();

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

    while (1)
    {
        display_fill(TFT_ORANGE);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_BLUE);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_GREEN);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_RED);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_fill(TFT_BLACK);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_draw_bmp(0, 0, "/spiffs/bmp_24.bmp");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}