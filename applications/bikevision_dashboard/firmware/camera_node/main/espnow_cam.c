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

#include <stdio.h>
#include <string.h>

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#include "espnow_link.h"

static const char *TAG = "espnow_cam";

#define FRAME_INTERVAL_MS 10     /* live: next frame right after display done */
#define FRAME_TIMEOUT_MS  2000   /* wait for display "done" (decode + draw) */

/* fps cap (2026-10-20, user: 12 fps): the next frame starts no sooner than
   1/12 s (83 ms) after this one started -> even picture rate, the camera
   rests when the link is faster. Set FRAME_CAP_FPS 0 to switch it off. */
#define FRAME_CAP_FPS     12

/* Logs (clean-up 2026-10-20): 0 = one summary line every 10 s (normal use),
   1 = also one line per frame + JPEG CHECK (for measurements). */
#define BV_FRAME_LOG      0
#define SUMMARY_PERIOD_US (10 * 1000000LL)

/* BikeVision 12fps plan, Phase B2 (measurement only).
 * 0 = normal live mode.
 * 1 = link-only test: take ONE picture, copy it, switch the camera OFF
 *     (esp_camera_deinit), then send that same copy again and again.
 *     Shows the pure radio speed without the camera working in the background.
 *     The TFT shows the same still picture; the camera log shows "LINK TEST". */
#define LINK_TEST_MODE    0

/* BikeVision 12fps plan, B9 (measurement only): look inside the JPEG.
 * Every frame since B4 has had exactly the same size, which a real JPEG
 * almost never does. This prints where the JPEG really ends (first FF D9
 * after the scan data starts), how many filler bytes sit before the last
 * FF D9, and the last 24 bytes in hex. Printed for the first 3 frames and
 * then every 200th frame. */
static void jpeg_debug(const uint8_t *b, size_t len, uint32_t n)
{
    if (len < 4) {
        return;
    }
    size_t sos = 0, first_eoi = 0;
    for (size_t i = 2; i + 1 < len; i++) {
        if (b[i] == 0xFF && b[i + 1] == 0xDA && sos == 0) {
            sos = i;
        }
        if (sos && i > sos && b[i] == 0xFF && b[i + 1] == 0xD9) {
            first_eoi = i + 2;   /* length up to and including FF D9 */
            break;
        }
    }
    /* filler bytes just before the final FF D9 */
    size_t ff = 0, zero = 0;
    for (size_t i = len - 3; i > 0 && b[i] == 0xFF; i--) {
        ff++;
    }
    for (size_t i = len - 3; i > 0 && b[i] == 0x00; i--) {
        zero++;
    }
    char hex[24 * 3 + 1];
    size_t start = len > 24 ? len - 24 : 0, k = 0;
    for (size_t i = start; i < len; i++) {
        k += snprintf(hex + k, sizeof(hex) - k, "%02X ", b[i]);
    }
    ESP_LOGW(TAG, "JPEG CHECK frame %lu: len %u, SOS at %u, first EOI ends at %u "
                  "(%u bytes after it), FF fill %u, 00 fill %u | tail: %s",
             (unsigned long)n, (unsigned)len, (unsigned)sos, (unsigned)first_eoi,
             (unsigned)(first_eoi ? len - first_eoi : 0),
             (unsigned)ff, (unsigned)zero, hex);
}

/* BikeVision 12fps plan, fix C1: send only the real JPEG.
 * B9 showed every frame was the real JPEG (~4.5 KB, ending at its first
 * FF D9 after the scan data) plus ~1.9 KB of old bytes left in the buffer
 * from an earlier frame, ending in that frame's FF D9. The driver reported
 * the whole thing as the frame length. The display ignores the extra bytes,
 * so they only cost radio time. Returns the length up to and including the
 * first FF D9 after the SOS marker, or len if it can't be found. */
static size_t jpeg_real_len(const uint8_t *b, size_t len)
{
    size_t sos = 0;
    for (size_t i = 2; i + 1 < len; i++) {
        if (sos == 0) {
            if (b[i] == 0xFF && b[i + 1] == 0xDA) {
                sos = i;
            }
        } else if (b[i] == 0xFF && b[i + 1] == 0xD9) {
            return i + 2;
        }
    }
    return len;
}

static void cam_task(void *arg)
{
    uint32_t sent = 0, ok = 0, failed = 0;
    uint32_t last_crc = 0, same_run = 0, same_total = 0;
    int64_t sum_t0 = esp_timer_get_time();
    uint32_t sum_n = 0, sum_bytes = 0, sum_fail = 0;

#if LINK_TEST_MODE
    /* B2: one picture, camera off, then send the copy forever */
    uint8_t *test_buf = NULL;
    size_t test_len = 0;
    while (!test_buf) {
        if (!espnow_link_is_up()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        camera_fb_t *fb0 = esp_camera_fb_get();
        if (fb0 && fb0->format == PIXFORMAT_JPEG && fb0->len > 0) {
            test_buf = heap_caps_malloc(fb0->len, MALLOC_CAP_SPIRAM);
            if (test_buf) {
                memcpy(test_buf, fb0->buf, fb0->len);
                test_len = fb0->len;
            }
        }
        if (fb0) {
            esp_camera_fb_return(fb0);
        }
    }
    esp_camera_deinit();
    ESP_LOGW(TAG, "LINK TEST: camera OFF, sending the same %u B picture again and again",
             (unsigned)test_len);
    while (true) {
        if (!espnow_link_is_up()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        espnow_link_frame_stats_t st = {0};
        esp_err_t err = espnow_link_send_frame(test_buf, test_len,
                                               FRAME_TIMEOUT_MS, &st);
        sent++;
        if (err == ESP_OK) {
            ok++;
            ESP_LOGI(TAG, "LINK TEST Frame %lu: %lu B, %u chunks, %u retries, sent %lu ms "
                          "| per chunk: lock %lu call %lu wait %lu us, max %lu us (ok %lu/%lu)",
                     (unsigned long)st.frame_id, (unsigned long)st.bytes, st.chunks,
                     st.retries, (unsigned long)st.send_ms,
                     (unsigned long)(st.lock_us / st.chunks),
                     (unsigned long)(st.call_us / st.chunks),
                     (unsigned long)(st.wait_us / st.chunks),
                     (unsigned long)st.max_chunk_us,
                     (unsigned long)ok, (unsigned long)sent);
        } else {
            failed++;
            ESP_LOGW(TAG, "LINK TEST Frame %lu failed: %s (failed %lu/%lu)",
                     (unsigned long)st.frame_id, esp_err_to_name(err),
                     (unsigned long)failed, (unsigned long)sent);
        }
        vTaskDelay(pdMS_TO_TICKS(FRAME_INTERVAL_MS));
    }
#endif

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

        if (BV_FRAME_LOG && (sent < 3 || sent % 200 == 0)) {   /* B9 measurement */
            jpeg_debug(fb->buf, fb->len, sent + 1);
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
        size_t send_len = jpeg_real_len(fb->buf, fb->len);   /* fix C1 */
        esp_err_t err = espnow_link_send_frame(fb->buf, send_len,
                                               FRAME_TIMEOUT_MS, &st);
        esp_camera_fb_return(fb);
        sent++;

        if (err == ESP_OK) {
            ok++;
            sum_n++;
            sum_bytes += st.bytes;
            if (BV_FRAME_LOG) ESP_LOGI(TAG, "Frame %lu: %lu B, capture %lu ms, %u chunks, %u retries, "
                          "sent %lu ms, display done %lu ms%s (ok %lu/%lu, same %lu) "
                          "| per chunk: lock %lu call %lu wait %lu us, max %lu us",
                     (unsigned long)st.frame_id, (unsigned long)st.bytes,
                     (unsigned long)cap_ms, st.chunks, st.retries,
                     (unsigned long)st.send_ms, (unsigned long)st.total_ms,
                     same ? " | SAME PICTURE as previous" : "",
                     (unsigned long)ok, (unsigned long)sent,
                     (unsigned long)same_total,
                     (unsigned long)(st.chunks ? st.lock_us / st.chunks : 0),
                     (unsigned long)(st.chunks ? st.call_us / st.chunks : 0),
                     (unsigned long)(st.chunks ? st.wait_us / st.chunks : 0),
                     (unsigned long)st.max_chunk_us);
        } else {
            failed++;
            sum_fail++;
            ESP_LOGW(TAG, "Frame %lu failed: %s (failed %lu/%lu)",
                     (unsigned long)st.frame_id, esp_err_to_name(err),
                     (unsigned long)failed, (unsigned long)sent);
        }
        if (same_run == 10) {
            ESP_LOGW(TAG, "Camera returned the same picture 10 times in a row "
                          "(frozen image)");
        }

        vTaskDelay(pdMS_TO_TICKS(FRAME_INTERVAL_MS));

        /* one summary line every 10 s */
        int64_t now_sum = esp_timer_get_time();
        if (now_sum - sum_t0 >= SUMMARY_PERIOD_US) {
            uint32_t dt_ms = (uint32_t)((now_sum - sum_t0) / 1000);
            uint32_t fps10 = dt_ms ? (uint32_t)((uint64_t)sum_n * 10000 / dt_ms) : 0;
            ESP_LOGI(TAG, "Summary %lu ms: %lu frames, %lu.%lu fps, avg %lu B, failed %lu "
                          "(total ok %lu / sent %lu)",
                     (unsigned long)dt_ms, (unsigned long)sum_n,
                     (unsigned long)(fps10 / 10), (unsigned long)(fps10 % 10),
                     (unsigned long)(sum_n ? sum_bytes / sum_n : 0),
                     (unsigned long)sum_fail, (unsigned long)ok, (unsigned long)sent);
            sum_t0 = now_sum;
            sum_n = 0;
            sum_bytes = 0;
            sum_fail = 0;
        }

#if FRAME_CAP_FPS > 0
        /* fps cap: wait until 1/FRAME_CAP_FPS s after this frame's capture
           started (t0). Sleep in ticks, then wait the last < 11 ms exactly. */
        int64_t next_us = t0 + 1000000LL / FRAME_CAP_FPS;
        int64_t now_us;
        while ((now_us = esp_timer_get_time()) < next_us) {
            int64_t left_us = next_us - now_us;
            if (left_us > (int64_t)portTICK_PERIOD_MS * 1000 + 1000) {
                vTaskDelay(1);
            } else {
                esp_rom_delay_us((uint32_t)left_us);
            }
        }
#endif
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