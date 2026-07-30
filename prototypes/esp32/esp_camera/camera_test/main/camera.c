#include "camera.h"

#include "esp_camera.h"
#include "esp_log.h"

static const char *TAG = "CAMERA";

esp_err_t camera_init(void)
{
    camera_config_t config = {
        // LEDC
        .ledc_channel = LEDC_CHANNEL_0,
        .ledc_timer = LEDC_TIMER_0,

        // Camera Pins (AI Thinker ESP32-CAM)
        .pin_d0 = 5,
        .pin_d1 = 18,
        .pin_d2 = 19,
        .pin_d3 = 21,
        .pin_d4 = 36,
        .pin_d5 = 39,
        .pin_d6 = 34,
        .pin_d7 = 35,

        .pin_xclk = 0,
        .pin_pclk = 22,
        .pin_vsync = 25,
        .pin_href = 23,

        .pin_sccb_sda = 26,
        .pin_sccb_scl = 27,

        .pin_pwdn = 32,
        .pin_reset = -1,

        // XCLK
        .xclk_freq_hz = 20000000,

        // Pixel Format
        .pixel_format = PIXFORMAT_JPEG,

        // Image Size
        .frame_size = FRAMESIZE_VGA,

        // JPEG Quality (0-63)
        .jpeg_quality = 12,

        // Number of Frame Buffers
        .fb_count = 1,

        .grab_mode = CAMERA_GRAB_WHEN_EMPTY};

    esp_err_t err = esp_camera_init(&config);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed (0x%x)", err);
        return err;
    }
    sensor_t *s = esp_camera_sensor_get();

    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_saturation(s, 1);
    s->set_whitebal(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    
    ESP_LOGI(TAG, "Camera initialized successfully");

    return ESP_OK;
}