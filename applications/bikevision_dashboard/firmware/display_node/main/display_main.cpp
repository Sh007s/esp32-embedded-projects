/*
 * display_main - BikeVision display board: live camera frames on the TFT.
 *
 * Receives camera JPEG frames over ESP-NOW and draws them on the TFT.
 *
 *  - Nothing is stored: no flash, no SPIFFS, no SD. The same 40 KB of RAM
 *    as before, now split in two fixed halves (BikeVision 4.6 overlap):
 *      20 KB receive buffer inside espnow_link (next frame arrives here)
 *      20 KB draw buffer here (frame being drawn). Both are reused.
 *  - Drawing uses the baseline TFT V1.0 function drawJpegBuffer() (made
 *    public in this application copy of tft.h). No full framebuffer.
 *  - Only one task touches the TFT (display_task). The ESP-NOW callbacks just
 *    pass work to it.
 *  - Overlap: on_frame copies the received frame into the draw buffer and
 *    returns at once, so the camera gets "done" and sends the next frame
 *    WHILE this one is drawn. If the draw buffer is still busy, on_frame
 *    waits for it first (so at most one frame waits, never a backlog).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "tft.h"

#include "espnow_link.h"

static const char *TAG = "display";

#define FRAME_RAM        (40 * 1024)  /* total RAM for frames, as before */
#define MAX_FRAME_SIZE   (FRAME_RAM / 2) /* 20 KB receive (espnow_link) + 20 KB draw (here) */
#define TFT_SPI_HZ       50000000     /* same as V1.0 test */
#define TFT_ROTATION     3            /* landscape 320x240, same as V1.0 test (1 = turned 180 deg) */
#define DRAW_WAIT_MS     3000         /* on_frame waits this long for the draw buffer */

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
    uint32_t       wait_ms;   /* how long on_frame waited for the draw buffer */
    uint8_t        channel;
} display_msg_t;

static QueueHandle_t     s_queue;
static SemaphoreHandle_t s_draw_free;   /* given = draw buffer is free */
static uint8_t          *s_draw_buf;    /* 20 KB, allocated once */

/* ---------------------------------------------------------------- */
/* JPEG -> TFT: baseline TFT V1.0 function                           */
/* ---------------------------------------------------------------- */

/* drawJpegBuffer() is the V1.0 decoder path (same esp_jpeg stream decode,
   block by block straight to the TFT). In this application copy of tft.h it
   was moved from private to public; tft.cpp is unchanged. */
static bool draw_jpeg(const uint8_t *jpg, size_t len)
{
    return tft.drawJpegBuffer(jpg, len, 0, 0, 0, 0);
}

/* ---------------------------------------------------------------- */
/* Start screen: shown at power-on and whenever the camera is lost,  */
/* until the first camera frame arrives.                             */
/* ---------------------------------------------------------------- */

/* Font is 5x7 in a 6x8 cell, scaled by text size. */
#define CHAR_W(size) (6 * (size))
#define START_BG      0x0841            /* very dark blue-grey */
#define STATUS_Y      168
#define STATUS_H      24
#define DOT_X         306
#define DOT_Y         (STATUS_Y + 8)

static bool s_on_start_screen = false;  /* true until a frame is drawn */
static bool s_dot_on = false;

static void text_center(const char *s, uint8_t size, int16_t y, uint16_t color)
{
    int16_t w = (int16_t)strlen(s) * CHAR_W(size);
    int16_t x = (tft.width() - w) / 2;
    tft.setTextSize(size);
    tft.setTextColor(color);
    tft.setCursor(x < 0 ? 0 : x, y);
    tft.writeText(s);
}

/* Status line near the bottom; redrawn without clearing the whole screen. */
static void start_status(const char *text, uint16_t color)
{
    tft.fillRect(0, STATUS_Y - 4, tft.width(), STATUS_H, START_BG);
    text_center(text, 2, STATUS_Y, color);
}

static void show_start_screen(const char *status, uint16_t status_color)
{
    const int16_t W = tft.width();
    tft.fillScreen(START_BG);

    /* Frame */
    tft.drawRoundRect(6, 6, W - 12, tft.height() - 12, 10, TFT_DARKGREY);

    /* Title: "Bike" white + "Vision" cyan, size 4 (24 px per char) */
    const uint8_t ts = 4;
    int16_t title_w = 10 * CHAR_W(ts);
    int16_t tx = (W - title_w) / 2;
    tft.setTextSize(ts);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(tx, 52);
    tft.writeText("Bike");
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(tx + 4 * CHAR_W(ts), 52);
    tft.writeText("Vision");

    /* Underline + subtitle */
    tft.fillRect(tx, 92, title_w, 3, TFT_CYAN);
    text_center("Smart Bike Dashboard", 2, 108, TFT_LIGHTGREY);

    /* Status line + version */
    start_status(status, status_color);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(14, tft.height() - 20);
    tft.writeText("ESP-NOW live camera");
    tft.setCursor(W - 14 - 6 * 6, tft.height() - 20);
    tft.writeText("v0.2");

    s_on_start_screen = true;
    s_dot_on = false;
}

/* Small blinking dot next to the status, so the screen visibly "lives". */
static void start_blink(void)
{
    s_dot_on = !s_dot_on;
    tft.fillCircle(DOT_X, DOT_Y, 3, s_dot_on ? TFT_CYAN : START_BG);
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
        /* Wake every 500 ms so the start screen dot can blink */
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(500)) != pdTRUE) {
            if (s_on_start_screen) {
                start_blink();
            }
            continue;
        }

        switch (msg.type) {
        case MSG_FRAME: {
            int64_t t0 = esp_timer_get_time();
            bool ok = draw_jpeg(msg.data, msg.len);
            s_on_start_screen = false;      /* camera picture now on screen */
            int64_t t1 = esp_timer_get_time();
            xSemaphoreGive(s_draw_free);    /* next frame may be copied in */

            uint32_t gap_ms = last_us ? (uint32_t)((t0 - last_us) / 1000) : 0;
            last_us = t0;
            if (ok) {
                drawn++;
            } else {
                failed++;
            }
            ESP_LOGI(TAG, "Frame %lu: %u B, draw %s %lu ms, %lu ms since last, "
                          "waited %lu ms, free heap %u (drawn %lu, failed %lu)",
                     (unsigned long)msg.frame_id, (unsigned)msg.len,
                     ok ? "OK" : "FAILED",
                     (unsigned long)((t1 - t0) / 1000),
                     (unsigned long)gap_ms,
                     (unsigned long)msg.wait_ms,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                     (unsigned long)drawn, (unsigned long)failed);
            break;
        }
        case MSG_LINK_UP:
            ESP_LOGI(TAG, "Camera link UP (channel %u)", msg.channel);
            if (!s_on_start_screen) {
                show_start_screen("Camera found", TFT_GREEN);
            } else {
                start_status("Camera found", TFT_GREEN);
            }
            break;
        case MSG_LINK_DOWN:
            ESP_LOGW(TAG, "Camera link DOWN");
            show_start_screen("Camera lost - searching", TFT_ORANGE);
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

    /* Wait until the previous frame has been drawn (draw buffer free).
       Usually it already is, or it frees within a few ms. */
    int64_t w0 = esp_timer_get_time();
    if (xSemaphoreTake(s_draw_free, pdMS_TO_TICKS(DRAW_WAIT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Frame %lu: draw buffer busy for %d ms, skipped",
                 (unsigned long)frame_id, DRAW_WAIT_MS);
        return;
    }
    uint32_t wait_ms = (uint32_t)((esp_timer_get_time() - w0) / 1000);

    /* Copy out of the receive buffer, so espnow_link can take the next
       frame while this one is drawn. (len <= MAX_FRAME_SIZE: espnow_link
       rejects bigger frames.) */
    memcpy(s_draw_buf, data, len);

    display_msg_t msg = {};
    msg.type = MSG_FRAME;
    msg.data = s_draw_buf;
    msg.len = len;
    msg.frame_id = frame_id;
    msg.wait_ms = wait_ms;

    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Frame %lu: display queue full, skipped",
                 (unsigned long)frame_id);
        xSemaphoreGive(s_draw_free);
        return;
    }
    /* Return now: espnow_link sends FRAME_ACK and the camera sends the
       next frame while this one is drawn. */
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

    /* drawJpegBuffer() logs ~7 info lines per frame; keep only warnings
       so the serial log does not slow the live picture down. */
    esp_log_level_set("TFT_JPEG", ESP_LOG_WARN);

    tft.begin();                     /* V1.0 default pins */
    tft.setFrequency(TFT_SPI_HZ);
    tft.setRotation(TFT_ROTATION);
    ESP_LOGI(TAG, "TFT %dx%d", tft.width(), tft.height());

    s_queue = xQueueCreate(4, sizeof(display_msg_t));
    s_draw_free = xSemaphoreCreateBinary();
    s_draw_buf = (uint8_t *)malloc(MAX_FRAME_SIZE);
    if (!s_queue || !s_draw_free || !s_draw_buf) {
        ESP_LOGE(TAG, "Out of memory for the draw buffer");
        return;
    }
    xSemaphoreGive(s_draw_free);     /* draw buffer starts free */
    ESP_LOGI(TAG, "Draw buffer %u B in RAM (reused, nothing stored in flash)",
             (unsigned)MAX_FRAME_SIZE);
    xTaskCreate(display_task, "display", 8192, NULL, 5, NULL);

    show_start_screen("Searching for camera", TFT_YELLOW);

    ESP_ERROR_CHECK(espnow_link_radio_start(0));

    espnow_link_config_t cfg = {};
    cfg.role = ESPNOW_LINK_RECEIVER;
    cfg.channel = 0;                  /* scan 1..13, follow the camera */
    cfg.max_frame_size = MAX_FRAME_SIZE;
    cfg.on_frame = on_frame;
    cfg.on_state = on_state;
    ESP_ERROR_CHECK(espnow_link_start(&cfg));

}