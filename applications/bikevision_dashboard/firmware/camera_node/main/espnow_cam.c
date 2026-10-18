/*
 * espnow_cam - camera side of the camera -> display link.
 *
 * Live mode: take one fresh JPEG, send it, wait until the display reports
 * "done", wait FRAME_INTERVAL_MS, repeat.
 *
 * Diagnostic (Phase 2): every frame's CRC32 is compared with the previous
 * frame. Identical CRC = the camera returned the same picture again
 * (frozen image). Capture time and send time are logged separately.
 */
#include "espnow_cam.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_camera.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"

#include "espnow_link.h"

static const char *TAG = "espnow_cam";

#define FRAME_INTERVAL_MS 10     /* live: next frame right after display done */
#define FRAME_TIMEOUT_MS  2000   /* wait for display "done" (decode + draw) */

static void cam_task(void *arg)
{
    uint32_t sent = 0, ok = 0, failed = 0;
    uint32_t last_crc = 0, same_run = 0, same_total = 0;

    while (true) {
        if (!espnow_link_is_up()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* camera.c uses 2 buffers + CAMERA_GRAB_LATEST: the driver keeps
           capturing in the background, so one get returns the newest
           picture without waiting (no discard step needed any more).
           The CRC check below proves the picture is not a repeat. */
        int64_t t0 = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        uint32_t cap_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);

        if (!fb || fb->format != PIXFORMAT_JPEG || fb->len == 0) {
            if (fb) {
                esp_camera_fb_return(fb);
            }
            ESP_LOGW(TAG, "Camera capture failed");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        /* Frozen-picture check */
        uint32_t crc = esp_rom_crc32_le(0, fb->buf, fb->len);
        bool same = (sent > 0 && crc == last_crc);
        if (same) {
            same_run++;
            same_total++;
        } else {
            same_run = 0;
        }
        last_crc = crc;

        espnow_link_frame_stats_t st = {0};
        esp_err_t err = espnow_link_send_frame(fb->buf, fb->len,
                                               FRAME_TIMEOUT_MS, &st);
        esp_camera_fb_return(fb);
        sent++;

        if (err == ESP_OK) {
            ok++;
            ESP_LOGI(TAG, "Frame %lu: %lu B, capture %lu ms, %u chunks, %u retries, "
                          "sent %lu ms, display done %lu ms%s (ok %lu/%lu, same %lu)",
                     (unsigned long)st.frame_id, (unsigned long)st.bytes,
                     (unsigned long)cap_ms, st.chunks, st.retries,
                     (unsigned long)st.send_ms, (unsigned long)st.total_ms,
                     same ? " | SAME PICTURE as previous" : "",
                     (unsigned long)ok, (unsigned long)sent,
                     (unsigned long)same_total);
        } else {
            failed++;
            ESP_LOGW(TAG, "Frame %lu failed: %s (failed %lu/%lu)",
                     (unsigned long)st.frame_id, esp_err_to_name(err),
                     (unsigned long)failed, (unsigned long)sent);
        }
        if (same_run == 10) {
            ESP_LOGW(TAG, "Camera returned the same picture 10 times in a row "
                          "(frozen image)");
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
    /* Priority 5 = same as the web stream task, so both get camera frames */
    xTaskCreate(cam_task, "espnow_cam", 4096, NULL, 5, NULL);
    return ESP_OK;
}