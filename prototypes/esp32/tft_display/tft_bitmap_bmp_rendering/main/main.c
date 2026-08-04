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

    while (1)
    {
        display_draw_bmp(0, 0, "/spiffs/red.bmp");
        display_draw_string(25, 100, "RED", TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_draw_bmp(0, 0, "/spiffs/green.bmp");
        display_draw_string(25, 100, "GREEN", TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(2000));

        display_draw_bmp(0, 0, "/spiffs/blue.bmp");
        display_draw_string(25, 100, "BLUE", TFT_WHITE);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}