/*
 * espnow_cam - camera side of the camera -> display link.
 *
 * Step 2 of the integration (single frames): every FRAME_INTERVAL_MS take
 * one fresh JPEG from the camera and send it to the display. The next frame
 * is only taken after the display reports the previous one done.
 *
 * The web page (port 80/81) keeps working; both share the camera.
 */
#include "espnow_cam.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_camera.h"
#include "esp_log.h"

#include "espnow_link.h"

static const char *TAG = "espnow_cam";

#define FRAME_INTERVAL_MS 10     /* live: next frame right after display done */
#define FRAME_TIMEOUT_MS  2000   /* wait for display "done" (decode + draw) */

static void cam_task(void *arg)
{
    uint32_t sent = 0, ok = 0, failed = 0;

    while (true) {
        if (!espnow_link_is_up()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* Discard the buffered (possibly old) frame, take a fresh one. */
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            esp_camera_fb_return(fb);
        }
        fb = esp_camera_fb_get();
        if (!fb || fb->format != PIXFORMAT_JPEG || fb->len == 0) {
            if (fb) {
                esp_camera_fb_return(fb);
            }
            ESP_LOGW(TAG, "Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        espnow_link_frame_stats_t st = {0};
        esp_err_t err = espnow_link_send_frame(fb->buf, fb->len,
                                               FRAME_TIMEOUT_MS, &st);
        esp_camera_fb_return(fb);
        sent++;

        if (err == ESP_OK) {
            ok++;
            ESP_LOGI(TAG, "Frame %lu: %lu B, %u chunks, %u retries, sent %lu ms, "
                          "display done %lu ms (ok %lu/%lu)",
                     (unsigned long)st.frame_id, (unsigned long)st.bytes,
                     st.chunks, st.retries, (unsigned long)st.send_ms,
                     (unsigned long)st.total_ms,
                     (unsigned long)ok, (unsigned long)sent);
        } else {
            failed++;
            ESP_LOGW(TAG, "Frame %lu failed: %s (failed %lu/%lu)",
                     (unsigned long)st.frame_id, esp_err_to_name(err),
                     (unsigned long)failed, (unsigned long)sent);
        }

        vTaskDelay(pdMS_TO_TICKS(FRAME_INTERVAL_MS));
    }
}

esp_err_t espnow_cam_start(void)
{
    espnow_link_config_t cfg = {
        .role = ESPNOW_LINK_SENDER,
    };
    esp_err_t err = espnow_link_start(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "espnow_link_start failed: %s", esp_err_to_name(err));
        return err;
    }
    xTaskCreate(cam_task, "espnow_cam", 4096, NULL, 5, NULL);
    return ESP_OK;
}
