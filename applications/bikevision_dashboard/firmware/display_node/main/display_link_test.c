/*
 * display_link_test - TEMPORARY app_main for the display board (step 2).
 *
 * Receives camera frames over ESP-NOW and checks them; does NOT draw yet.
 * The TFT V1.0 files stay in main/ untouched; they are just not compiled in
 * this step (see main/CMakeLists.txt). Drawing is added in the next step.
 *
 * Checks per frame: CRC32 (done by espnow_link), JPEG start marker FF D8,
 * JPEG end marker FF D9, size.
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "espnow_link.h"

static const char *TAG = "display";

#define MAX_FRAME_SIZE (40 * 1024)   /* one reusable RAM buffer */

static uint32_t s_frames, s_bad_jpeg;
static int64_t  s_last_us;

static void on_frame(const uint8_t *data, size_t len, uint32_t frame_id)
{
    bool soi = len >= 2 && data[0] == 0xFF && data[1] == 0xD8;
    bool eoi = len >= 2 && data[len - 2] == 0xFF && data[len - 1] == 0xD9;
    int64_t now = esp_timer_get_time();
    uint32_t gap_ms = s_last_us ? (uint32_t)((now - s_last_us) / 1000) : 0;
    s_last_us = now;
    s_frames++;
    if (!soi || !eoi) {
        s_bad_jpeg++;
    }
    ESP_LOGI(TAG, "Frame %lu: %u B, JPEG start %s, end %s, %lu ms since last, "
                  "free heap %u (frames %lu, bad JPEG %lu)",
             (unsigned long)frame_id, (unsigned)len,
             soi ? "OK" : "BAD", eoi ? "OK" : "BAD",
             (unsigned long)gap_ms,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned long)s_frames, (unsigned long)s_bad_jpeg);
}

static void on_state(bool up, const uint8_t mac[6], uint8_t channel)
{
    ESP_LOGI(TAG, "Camera link %s (channel %u)", up ? "UP" : "DOWN", channel);
}

void app_main(void)
{
    ESP_ERROR_CHECK(espnow_link_radio_start(0));

    espnow_link_config_t cfg = {
        .role = ESPNOW_LINK_RECEIVER,
        .channel = 0,                 /* scan 1..13, follow the camera */
        .max_frame_size = MAX_FRAME_SIZE,
        .on_frame = on_frame,
        .on_state = on_state,
    };
    ESP_ERROR_CHECK(espnow_link_start(&cfg));
}
