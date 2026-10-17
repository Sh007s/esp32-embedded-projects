/*
 * display_main - BikeVision display board, step 3.
 *
 * Receives camera JPEG frames over ESP-NOW and draws them on the TFT.
 *
 *  - Nothing is stored: no flash, no SPIFFS, no SD. The frame lives only in
 *    the one reusable 40 KB RAM buffer inside espnow_link and is overwritten
 *    by the next frame.
 *  - No full framebuffer: the JPEG decoder hands out small blocks
 *    (max 16x16 px) which are converted to RGB565 and sent straight to the TFT.
 *  - TFT V1.0 files (tft.cpp, tft.h, font5x7.h, tft_image_tests.cpp) are NOT
 *    modified. Only their public API is used: begin, setFrequency,
 *    setRotation, fillScreen, startWrite/endWrite, setAddrWindow, writePixels,
 *    text functions.
 *  - Only one task touches the TFT (display_task). The ESP-NOW callbacks just
 *    pass work to it. on_frame waits until the frame is drawn, so the camera
 *    gets "done" only after the picture is on screen.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "jpeg_decoder.h"
#include "tft.h"

#include "espnow_link.h"

static const char *TAG = "display";

#define MAX_FRAME_SIZE   (40 * 1024)  /* one reusable RAM buffer (in espnow_link) */
#define TFT_SPI_HZ       50000000     /* same as V1.0 test */
#define TFT_ROTATION     3            /* landscape 320x240, same as V1.0 test */
#define DRAW_WAIT_MS     3000         /* on_frame waits this long for the draw */
#define MAX_BLOCK_PIXELS (16 * 16)    /* largest JPEG MCU block */

static TFT tft(0);

/* ---------------------------------------------------------------- */
/* Messages from the ESP-NOW callbacks to the display task          */
/* ---------------------------------------------------------------- */

typedef enum { MSG_FRAME = 1, MSG_LINK_UP, MSG_LINK_DOWN } msg_type_t;

typedef struct {
    msg_type_t     type;
    const uint8_t *data;
    size_t         len;
    uint32_t       frame_id;
    uint8_t        channel;
} display_msg_t;

static QueueHandle_t     s_queue;
static SemaphoreHandle_t s_frame_done;

/* ---------------------------------------------------------------- */
/* JPEG block -> TFT                                                 */
/* ---------------------------------------------------------------- */

static uint16_t s_block[MAX_BLOCK_PIXELS];   /* 512 B, reused for every block */

typedef struct {
    uint16_t x, y;      /* top-left on screen */
    bool     error;
} draw_ctx_t;

static int jpeg_block_cb(const uint8_t *bitmap,
                         const esp_jpeg_rect_t *rect,
                         void *user_ctx)
{
    draw_ctx_t *ctx = (draw_ctx_t *)user_ctx;
    if (!bitmap || !rect || !ctx) {
        return 0;
    }

    uint16_t w = rect->right - rect->left + 1;
    uint16_t h = rect->bottom - rect->top + 1;
    uint16_t dx = ctx->x + rect->left;
    uint16_t dy = ctx->y + rect->top;

    if ((uint32_t)w * h > MAX_BLOCK_PIXELS ||
        dx + w > tft.width() || dy + h > tft.height()) {
        ctx->error = true;
        return 0;
    }

    /* Decoder output is RGB888 (3 bytes per pixel) -> RGB565 */
    const uint8_t *src = bitmap;
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
        uint8_t r = *src++;
        uint8_t g = *src++;
        uint8_t b = *src++;
        s_block[i] = tft.color565(r, g, b);
    }

    tft.setAddrWindow(dx, dy, w, h);
    tft.writePixels(s_block, (uint32_t)w * h);
    return 1;
}

static bool draw_jpeg(const uint8_t *jpg, size_t len)
{
    esp_jpeg_image_cfg_t cfg = {};
    cfg.indata = (uint8_t *)jpg;
    cfg.indata_size = len;
    cfg.out_format = JPEG_IMAGE_FORMAT_RGB888;
    cfg.out_scale = JPEG_IMAGE_SCALE_0;

    esp_jpeg_image_output_t info = {};
    esp_err_t err = esp_jpeg_get_image_info(&cfg, &info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "JPEG info failed: %s", esp_err_to_name(err));
        return false;
    }
    if (info.width > tft.width() || info.height > tft.height()) {
        ESP_LOGW(TAG, "JPEG %ux%u bigger than TFT %dx%d",
                 info.width, info.height, tft.width(), tft.height());
        return false;
    }

    /* Centre the picture (320x240 fills the screen exactly). */
    draw_ctx_t ctx = {};
    ctx.x = (tft.width() - info.width) / 2;
    ctx.y = (tft.height() - info.height) / 2;

    tft.startWrite();
    err = esp_jpeg_decode_stream((uint8_t *)jpg, len, JPEG_IMAGE_SCALE_0,
                                 jpeg_block_cb, &ctx);
    tft.endWrite();

    if (err != ESP_OK || ctx.error) {
        ESP_LOGW(TAG, "JPEG decode failed: %s%s", esp_err_to_name(err),
                 ctx.error ? " (block outside screen)" : "");
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------- */
/* Status text                                                       */
/* ---------------------------------------------------------------- */

static void show_status(const char *line1, const char *line2)
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 100);
    tft.writeText(line1);
    if (line2) {
        tft.setTextSize(1);
        tft.setCursor(10, 130);
        tft.writeText(line2);
    }
}

/* ---------------------------------------------------------------- */
/* Display task: the only task that touches the TFT                  */
/* ---------------------------------------------------------------- */

static void display_task(void *arg)
{
    uint32_t drawn = 0, failed = 0;
    int64_t last_us = 0;
    display_msg_t msg;

    while (true) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (msg.type) {
        case MSG_FRAME: {
            int64_t t0 = esp_timer_get_time();
            bool ok = draw_jpeg(msg.data, msg.len);
            int64_t t1 = esp_timer_get_time();
            xSemaphoreGive(s_frame_done);   /* camera may send the next one */

            uint32_t gap_ms = last_us ? (uint32_t)((t0 - last_us) / 1000) : 0;
            last_us = t0;
            if (ok) {
                drawn++;
            } else {
                failed++;
            }
            ESP_LOGI(TAG, "Frame %lu: %u B, draw %s %lu ms, %lu ms since last, "
                          "free heap %u (drawn %lu, failed %lu)",
                     (unsigned long)msg.frame_id, (unsigned)msg.len,
                     ok ? "OK" : "FAILED",
                     (unsigned long)((t1 - t0) / 1000),
                     (unsigned long)gap_ms,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                     (unsigned long)drawn, (unsigned long)failed);
            break;
        }
        case MSG_LINK_UP:
            ESP_LOGI(TAG, "Camera link UP (channel %u)", msg.channel);
            show_status("Camera connected", "Waiting for first frame...");
            break;
        case MSG_LINK_DOWN:
            ESP_LOGW(TAG, "Camera link DOWN");
            show_status("No camera", "Searching...");
            break;
        }
    }
}

/* ---------------------------------------------------------------- */
/* ESP-NOW callbacks (run in espnow_link tasks; never touch the TFT) */
/* ---------------------------------------------------------------- */

static void on_frame(const uint8_t *data, size_t len, uint32_t frame_id)
{
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8 ||
        data[len - 2] != 0xFF || data[len - 1] != 0xD9) {
        ESP_LOGW(TAG, "Frame %lu: not a complete JPEG, skipped",
                 (unsigned long)frame_id);
        return;
    }

    display_msg_t msg = {};
    msg.type = MSG_FRAME;
    msg.data = data;
    msg.len = len;
    msg.frame_id = frame_id;

    xSemaphoreTake(s_frame_done, 0);   /* clear any old "done" */
    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Frame %lu: display busy, skipped",
                 (unsigned long)frame_id);
        return;
    }
    /* Wait until drawn: the buffer must not change while it is decoded,
       and the camera gets FRAME_ACK only after this returns. */
    if (xSemaphoreTake(s_frame_done, pdMS_TO_TICKS(DRAW_WAIT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Frame %lu: draw took more than %d ms",
                 (unsigned long)frame_id, DRAW_WAIT_MS);
    }
}

static void on_state(bool up, const uint8_t mac[6], uint8_t channel)
{
    display_msg_t msg = {};
    msg.type = up ? MSG_LINK_UP : MSG_LINK_DOWN;
    msg.channel = channel;
    xQueueSend(s_queue, &msg, 0);
}

/* ---------------------------------------------------------------- */

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "BikeVision display (step 3: draw camera frames)");

    tft.begin();                     /* V1.0 default pins */
    tft.setFrequency(TFT_SPI_HZ);
    tft.setRotation(TFT_ROTATION);
    ESP_LOGI(TAG, "TFT %dx%d", tft.width(), tft.height());

    s_queue = xQueueCreate(4, sizeof(display_msg_t));
    s_frame_done = xSemaphoreCreateBinary();
    xTaskCreate(display_task, "display", 8192, NULL, 5, NULL);

    show_status("BikeVision", "Waiting for camera...");

    ESP_ERROR_CHECK(espnow_link_radio_start(0));

    espnow_link_config_t cfg = {};
    cfg.role = ESPNOW_LINK_RECEIVER;
    cfg.channel = 0;                  /* scan 1..13, follow the camera */
    cfg.max_frame_size = MAX_FRAME_SIZE;
    cfg.on_frame = on_frame;
    cfg.on_state = on_state;
    ESP_ERROR_CHECK(espnow_link_start(&cfg));
}