#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "camera.h"
#include "esp_camera.h"
#include "sdcard.h"

#define LED_GPIO GPIO_NUM_4

static const char *TAG = "MAIN";

void app_main(void)
{
    // Configure GPIO4 (Flash LED)
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    printf("\n");
    printf("=================================\n");
    printf("      ESP32-CAM Camera Test\n");
    printf("=================================\n");

    ESP_LOGI(TAG, "Initializing camera...");

    esp_err_t ret = camera_init();
    vTaskDelay(pdMS_TO_TICKS(500));

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera initialization failed! Error = 0x%x", ret);

        while (1)
        {
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(200));

            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    ESP_LOGI(TAG, "Camera initialized successfully.");

    if (sdcard_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "SD card initialization failed");
        return;
    }

    for (int i = 0; i < 5; i++)
    {
        camera_fb_t *tmp = esp_camera_fb_get();
        if (tmp)
        {
            esp_camera_fb_return(tmp);
        }
    }

    // Capture the actual image
    camera_fb_t *fb = esp_camera_fb_get();

    if (fb == NULL)
    {
        ESP_LOGE(TAG, "Failed to capture image");
    }
    else
    {
        ESP_LOGI(TAG, "JPEG Header: %02X %02X",
                 fb->buf[0], fb->buf[1]);

        ESP_LOGI(TAG, "Image captured successfully!");
        ESP_LOGI(TAG, "Width  : %d", fb->width);
        ESP_LOGI(TAG, "Height : %d", fb->height);
        ESP_LOGI(TAG, "Length : %u bytes", (unsigned int)fb->len);

        // Save image to SD card
        FILE *f = fopen("/sdcard/photo.jpg", "wb");

        if (f == NULL)
        {
            ESP_LOGE(TAG, "Failed to open file for writing");
        }
        else
        {
            size_t written = fwrite(fb->buf, 1, fb->len, f);
            fclose(f);

            if (written == fb->len)
            {
                ESP_LOGI(TAG, "Photo saved successfully!");
                ESP_LOGI(TAG, "File: /sdcard/photo.jpg");
            }
            else
            {
                ESP_LOGE(TAG, "Failed to write complete image");
            }
        }

        // Return frame buffer
        esp_camera_fb_return(fb);
    }

    while (1)
    {
        ESP_LOGI(TAG, "LED ON");
        // gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "LED OFF");
        // gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}