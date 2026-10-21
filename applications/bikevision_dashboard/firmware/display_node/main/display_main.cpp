/*
 * display_main - BikeVision display board: pages + live camera on the TFT.
 *
 * Pages (UI design "BikeVision Display UI"):
 *   1 Boot animation (~3 s): bike rides in, headlight blinks, title, loading bar
 *   2 Menu: 6 tiles (Camera, Ride, Location, Time, Cam info, Settings)
 *   3 Camera: live camera picture (full screen)
 *   4 Cam info: link + frame numbers, refreshed twice a second
 *   5 Time: time since power-on (real clock needs the GPS)
 *   6 Ride / Location / Settings: placeholders until GPS / sensors are fitted
 *
 * Control: rotary encoder (bv_input.c). Menu: turn = select tile, press = open.
 * Info pages: press = back to the menu. Camera page: hold the knob 2 s = back
 * (short press is kept free for the photo later). Hold 2 s works everywhere.
 * (The 12x12 buttons come later: BACK and a camera shortcut.)
 *
 * TFT self-check (BV_TFT_WATCH): a TFT that reset itself is set up again.
 *
 * Camera frames keep arriving on every page (the camera has no start/stop
 * command yet); they are drawn only on the Camera page.
 *
 * Memory, as before: 20 KB receive buffer inside the link + 20 KB draw buffer
 * here, both reused, nothing stored in flash. Only display_task touches the
 * TFT; the link callbacks and the encoder only send messages to it.
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
#include "bv_input.h"         /* rotary encoder (+ later 2 buttons) */
#include "bv_link_config.h"   /* BV_LINK_MODE: ESP-NOW (0) or Wi-Fi direct (1) */

static const char *TAG = "display";

#define FRAME_RAM        (40 * 1024)  /* total RAM for frames, as before */
#define MAX_FRAME_SIZE   (FRAME_RAM / 2) /* 20 KB receive (link) + 20 KB draw (here) */
#define TFT_SPI_HZ       50000000     /* same as V1.0 test */
#define TFT_ROTATION     3            /* landscape 320x240, same as V1.0 test (1 = turned 180 deg) */
#define DRAW_WAIT_MS     3000         /* on_frame waits this long for the draw buffer */

/* Logs (clean-up 2026-10-20): 0 = one summary line every 10 s (normal use),
   1 = also one line per frame (for measurements). Failed draws always log. */
#define BV_FRAME_LOG     0
#define SUMMARY_PERIOD_US (10 * 1000000LL)

/* 1 = encoder test only (every turn / press is logged, pages not used).
   0 = pages controlled by the encoder (every input is still logged once). */
#define BV_INPUT_TEST    0

#define BV_BOOT_ANIM     1            /* 0 = skip the boot animation */
#define UI_TICK_US       (500 * 1000LL)

/* TFT self-check: every 2 s read the ILI9341 power mode (0x0A) over MISO.
   If the TFT has reset itself (white screen after a power dip or a loose
   wire), set it up again and redraw the page. Switched off automatically
   when the TFT can't be read (e.g. MISO not connected). */
#define BV_TFT_WATCH     1
#define TFT_CHECK_US     (2000 * 1000LL)
#define TFT_READ_HZ      4000000      /* ILI9341 reads need a slow clock (~6 MHz max) */

static TFT tft(0);

/* ---------------------------------------------------------------- */
/* Messages from the link callbacks to the display task             */
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
static QueueHandle_t     s_in_q;        /* encoder events */
static SemaphoreHandle_t s_draw_free;   /* given = draw buffer is free */
static uint8_t          *s_draw_buf;    /* 20 KB, allocated once */

/* ---------------------------------------------------------------- */
/* Colours (RGB565) and text helpers                                 */
/* ---------------------------------------------------------------- */

#define C_BG       0x0000   /* black: the TN panel shows near-black as blue-grey */
#define C_BAR      0x0000   /* top bar */
#define C_TILE     0x10A2   /* tile fill, dark grey */
#define C_SEL      0x08C3   /* selected tile fill #0A1A1C */
#define C_LINE     0x39C7   /* grey border #3A3A3A */
#define C_DIM      0x7BEF   /* dim text #7B7D7B */
#define C_TEXT     TFT_WHITE
#define C_ACCENT   TFT_CYAN
#define C_OK       TFT_LIME
#define C_WARN     TFT_ORANGE
#define C_YELLOW   TFT_YELLOW

/* Font is 5x7 in a 6x8 cell, scaled by text size. */
#define CHAR_W(size) (6 * (size))
#define CHAR_H(size) (8 * (size))

static void text_at(int16_t x, int16_t y, uint8_t size, uint16_t color, const char *s)
{
    tft.setTextSize(size);
    tft.setTextColor(color);
    tft.setCursor(x < 0 ? 0 : x, y < 0 ? 0 : y);
    tft.writeText(s);
}

static void text_center(const char *s, uint8_t size, int16_t y, uint16_t color)
{
    int16_t w = (int16_t)strlen(s) * CHAR_W(size);
    text_at((tft.width() - w) / 2, y, size, color, s);
}

/* ---------------------------------------------------------------- */
/* State                                                             */
/* ---------------------------------------------------------------- */

typedef enum {
    PAGE_BOOT = 0, PAGE_MENU, PAGE_CAMERA, PAGE_RIDE, PAGE_LOCATION,
    PAGE_TIME, PAGE_CAMINFO, PAGE_SETTINGS,
} page_t;

static const char *PAGE_NAME[] = {
    "BOOT", "MENU", "CAMERA", "RIDE", "LOCATION", "TIME", "CAM INFO", "SETTINGS",
};

/* menu tiles, in screen order (2 columns x 3 rows) */
static const page_t TILE_PAGE[6] = {
    PAGE_CAMERA, PAGE_RIDE, PAGE_LOCATION, PAGE_TIME, PAGE_CAMINFO, PAGE_SETTINGS,
};

static page_t   s_page = PAGE_BOOT;
static int      s_sel = 0;                 /* selected menu tile */
static bool     s_link_up = false;
static uint8_t  s_channel = 0;
static bool     s_cam_waiting = false;     /* camera page open, no picture yet */
static bool     s_on_start_screen = false; /* camera page shows the link status screen */
static bool     s_dot_on = false;

/* numbers for the pages */
static uint32_t s_drawn = 0, s_failed = 0, s_rx_total = 0;
static uint32_t s_rx_window = 0;           /* frames received in the current 2 s window */
static int64_t  s_rx_window_t0 = 0;
static uint32_t s_rx_fps10 = 0;            /* received frames per second x10 */
static bool     s_tft_watch = false;       /* TFT self-check active */
static volatile bool s_show_frames = false;  /* true only on the Camera page (read by on_frame) */
static uint32_t s_last_fid = 0;            /* last camera frame id seen */
static uint32_t s_lost_total = 0;          /* frames lost on the way (frame-id gaps) */

/* 10 s summary (printed on every page) */
static int64_t  s_sum_t0;
static uint32_t s_sum_rx, s_sum_lost, s_sum_n, s_sum_draw_ms, s_sum_max_draw, s_sum_max_gap, s_sum_fail;
static uint32_t s_tft_resets = 0;          /* times the TFT had to be set up again */

/* ---------------------------------------------------------------- */
/* JPEG -> TFT: baseline TFT V1.0 function                           */
/* ---------------------------------------------------------------- */

static bool draw_jpeg(const uint8_t *jpg, size_t len)
{
    return tft.drawJpegBuffer(jpg, len, 0, 0, 0, 0);
}

/* ---------------------------------------------------------------- */
/* Camera page: link status screen (shown until the first picture)   */
/* ---------------------------------------------------------------- */

#define START_BG      C_BG
#define STATUS_Y      168
#define STATUS_H      24
#define DOT_X         306
#define DOT_Y         (STATUS_Y + 8)

static void start_status(const char *text, uint16_t color)
{
    tft.fillRect(0, STATUS_Y - 4, tft.width(), STATUS_H, START_BG);
    text_center(text, 2, STATUS_Y, color);
}

static void show_start_screen(const char *status, uint16_t status_color)
{
    const int16_t W = tft.width();
    tft.fillScreen(START_BG);
    tft.drawRoundRect(6, 6, W - 12, tft.height() - 12, 10, C_LINE);

    const uint8_t ts = 4;
    int16_t title_w = 10 * CHAR_W(ts);
    int16_t tx = (W - title_w) / 2;
    text_at(tx, 52, ts, TFT_WHITE, "Bike");
    text_at(tx + 4 * CHAR_W(ts), 52, ts, TFT_CYAN, "Vision");
    tft.fillRect(tx, 92, title_w, 3, TFT_CYAN);
    text_center("Live camera", 2, 108, TFT_LIGHTGREY);

    start_status(status, status_color);
    text_at(14, tft.height() - 20, 1, C_DIM, BV_LINK_MODE_NAME " link");
    text_at(W - 14 - 24 * 6, tft.height() - 20, 1, C_DIM, "hold knob 2 s = menu");

    s_on_start_screen = true;
    s_dot_on = false;
}

static void start_blink(void)
{
    s_dot_on = !s_dot_on;
    tft.fillCircle(DOT_X, DOT_Y, 3, s_dot_on ? TFT_CYAN : START_BG);
}

/* ---------------------------------------------------------------- */
/* Top bar (16 px) used on the menu and info pages                    */
/* ---------------------------------------------------------------- */

static char s_bar_cache[24];   /* top bar status as last drawn ("" = redraw) */

static void top_bar_status(void)
{
    /* right side: link state, redrawn only when the text changes */
    char buf[24];
    if (s_link_up) {
        snprintf(buf, sizeof(buf), "CAM %lu.%lu fps",
                 (unsigned long)(s_rx_fps10 / 10), (unsigned long)(s_rx_fps10 % 10));
    } else {
        snprintf(buf, sizeof(buf), "NO CAM");
    }
    if (strcmp(buf, s_bar_cache) == 0) {
        return;
    }
    strcpy(s_bar_cache, buf);
    tft.fillRect(220, 0, 100, 16, C_BAR);
    text_at(314 - (int16_t)strlen(buf) * 6, 4, 1, s_link_up ? C_OK : C_WARN, buf);
}

static void top_bar(const char *title)
{
    s_bar_cache[0] = 0;
    tft.fillRect(0, 0, 320, 16, C_BAR);
    text_at(6, 4, 1, C_TEXT, "BikeVision");
    text_center(title, 1, 4, C_ACCENT);
    top_bar_status();
}

/* ---------------------------------------------------------------- */
/* Menu: 2 columns x 3 rows of tiles                                 */
/* ---------------------------------------------------------------- */

#define TILE_W   149
#define TILE_H   61
static const int16_t TILE_X[2] = { 8, 163 };
static const int16_t TILE_Y[3] = { 22, 89, 156 };

static void icon(int i, int16_t ix, int16_t iy, uint16_t c, uint16_t bg)
{
    /* 26 x 24 px icon area at (ix, iy) */
    switch (i) {
    case 0: /* camera */
        tft.drawRoundRect(ix, iy + 5, 26, 18, 3, c);
        tft.fillRect(ix + 8, iy + 1, 10, 4, c);
        tft.drawCircle(ix + 13, iy + 14, 5, c);
        break;
    case 1: /* ride: speedometer */
        tft.drawCircle(ix + 13, iy + 17, 12, c);
        tft.fillRect(ix, iy + 18, 27, 13, bg);
        tft.drawLine(ix + 13, iy + 17, ix + 21, iy + 9, c);
        tft.fillCircle(ix + 13, iy + 17, 2, c);
        break;
    case 2: /* location pin */
        tft.fillCircle(ix + 13, iy + 8, 8, c);
        tft.fillTriangle(ix + 6, iy + 12, ix + 20, iy + 12, ix + 13, iy + 24, c);
        tft.fillCircle(ix + 13, iy + 8, 3, bg);
        break;
    case 3: /* time: clock */
        tft.drawCircle(ix + 13, iy + 12, 11, c);
        tft.drawLine(ix + 13, iy + 12, ix + 13, iy + 5, c);
        tft.drawLine(ix + 13, iy + 12, ix + 18, iy + 15, c);
        break;
    case 4: /* cam info: signal bars */
        tft.fillRect(ix + 2, iy + 17, 4, 6, c);
        tft.fillRect(ix + 9, iy + 12, 4, 11, c);
        tft.fillRect(ix + 16, iy + 7, 4, 16, c);
        tft.fillRect(ix + 23, iy + 2, 3, 21, c);
        break;
    default: /* settings: gear */
        tft.drawCircle(ix + 13, iy + 12, 8, c);
        tft.fillCircle(ix + 13, iy + 12, 3, c);
        tft.drawLine(ix + 13, iy, ix + 13, iy + 3, c);
        tft.drawLine(ix + 13, iy + 21, ix + 13, iy + 24, c);
        tft.drawLine(ix + 1, iy + 12, ix + 4, iy + 12, c);
        tft.drawLine(ix + 22, iy + 12, ix + 25, iy + 12, c);
        tft.drawLine(ix + 5, iy + 4, ix + 7, iy + 6, c);
        tft.drawLine(ix + 19, iy + 18, ix + 21, iy + 20, c);
        tft.drawLine(ix + 5, iy + 20, ix + 7, iy + 18, c);
        tft.drawLine(ix + 19, iy + 6, ix + 21, iy + 4, c);
        break;
    }
}

static void tile_value(int i, char *buf, size_t n, uint16_t *color)
{
    *color = C_DIM;
    switch (i) {
    case 0:
        if (s_link_up) {
            snprintf(buf, n, "LIVE %lu.%lu fps",
                     (unsigned long)(s_rx_fps10 / 10), (unsigned long)(s_rx_fps10 % 10));
            *color = C_OK;
        } else {
            snprintf(buf, n, "no camera");
            *color = C_WARN;
        }
        break;
    case 1: snprintf(buf, n, "needs GPS"); break;
    case 2: snprintf(buf, n, "needs GPS"); break;
    case 3: snprintf(buf, n, "since power on"); break;
    case 4: snprintf(buf, n, "%s", BV_LINK_MODE_NAME); *color = C_ACCENT; break;
    default: snprintf(buf, n, "brightness"); break;
    }
}

static char s_tile_cache[6][24];   /* tile value text as last drawn */

static void draw_tile_value(int i, bool force)
{
    int16_t x = TILE_X[i % 2], y = TILE_Y[i / 2];
    bool sel = (i == s_sel);
    char buf[24];
    uint16_t col;
    tile_value(i, buf, sizeof(buf), &col);
    if (!force && strcmp(buf, s_tile_cache[i]) == 0) {
        return;
    }
    strcpy(s_tile_cache[i], buf);
    tft.fillRect(x + 50, y + 38, TILE_W - 56, 10, sel ? C_SEL : C_TILE);
    text_at(x + 50, y + 38, 1, col, buf);
}

static void draw_tile(int i, bool sel)
{
    static const char *LABEL[6] = { "CAMERA", "RIDE", "LOCATION", "TIME", "CAM INFO", "SETTINGS" };
    int16_t x = TILE_X[i % 2], y = TILE_Y[i / 2];
    uint16_t bg = sel ? C_SEL : C_TILE;
    tft.fillRoundRect(x, y, TILE_W, TILE_H, 6, bg);
    if (sel) {
        tft.drawRoundRect(x, y, TILE_W, TILE_H, 6, C_ACCENT);
        tft.drawRoundRect(x + 1, y + 1, TILE_W - 2, TILE_H - 2, 5, C_ACCENT);
    } else {
        tft.drawRoundRect(x, y, TILE_W, TILE_H, 6, C_LINE);
    }
    icon(i, x + 12, y + 18, sel ? C_ACCENT : C_TEXT, bg);
    text_at(x + 50, y + 14, 2, C_TEXT, LABEL[i]);
    draw_tile_value(i, true);
}

static void show_menu(void)
{
    tft.fillScreen(C_BG);
    top_bar("MENU");
    for (int i = 0; i < 6; i++) {
        draw_tile(i, i == s_sel);
    }
    text_center("turn = select  press = open  hold = fix screen", 1, 226, C_DIM);
}

/* ---------------------------------------------------------------- */
/* Other pages                                                        */
/* ---------------------------------------------------------------- */

static void page_frame(const char *title)
{
    tft.fillScreen(C_BG);
    top_bar(title);
    text_center("press knob = back to menu", 1, 226, C_DIM);
}

static void show_placeholder(const char *title, const char *line1, const char *line2)
{
    page_frame(title);
    tft.drawRoundRect(20, 40, 280, 160, 8, C_LINE);
    text_center(title, 3, 70, C_TEXT);
    text_center(line1, 2, 120, C_YELLOW);
    text_center(line2, 1, 150, C_DIM);
}

/* Cam info: label column + value column, values redrawn every UI tick */
#define INFO_X_LABEL  24
#define INFO_X_VALUE  150
#define INFO_Y0       34
#define INFO_DY       24
static const char *INFO_LABEL[] = { "LINK", "STATUS", "FRAMES / S", "RECEIVED", "DRAWN", "FAILED", "FREE RAM", "UPTIME" };
/* (TFT self-check resets: in the 10 s Summary line and "TFT reset itself ..." log) */
#define INFO_ROWS (sizeof(INFO_LABEL) / sizeof(INFO_LABEL[0]))

static void uptime_text(char *buf, size_t n)
{
    uint32_t s = (uint32_t)(esp_timer_get_time() / 1000000);
    snprintf(buf, n, "%02lu:%02lu:%02lu", (unsigned long)(s / 3600),
             (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
}

static char s_info_cache[INFO_ROWS][32];   /* value text as last drawn */

static void info_values(bool force)
{
    char buf[32];
    for (size_t r = 0; r < INFO_ROWS; r++) {
        int16_t y = INFO_Y0 + (int16_t)r * INFO_DY;
        uint16_t col = C_TEXT;
        switch (r) {
        case 0: snprintf(buf, sizeof(buf), "%s ch %u", BV_LINK_MODE == BV_LINK_WIFI ? "Wi-Fi" : "ESP-NOW", s_channel); break;
        case 1: snprintf(buf, sizeof(buf), "%s", s_link_up ? "UP" : "DOWN"); col = s_link_up ? C_OK : C_WARN; break;
        case 2: snprintf(buf, sizeof(buf), "%lu.%lu", (unsigned long)(s_rx_fps10 / 10), (unsigned long)(s_rx_fps10 % 10)); break;
        case 3: snprintf(buf, sizeof(buf), "%lu lost %lu", (unsigned long)s_rx_total, (unsigned long)s_lost_total); col = s_lost_total ? C_YELLOW : C_TEXT; break;
        case 4: snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_drawn); break;
        case 5: snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_failed); col = s_failed ? C_WARN : C_TEXT; break;
        case 6: snprintf(buf, sizeof(buf), "%u KB", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024)); break;
        default: uptime_text(buf, sizeof(buf)); break;
        }
        if (!force && strcmp(buf, s_info_cache[r]) == 0) {
            continue;                       /* unchanged: no redraw, no flicker */
        }
        strcpy(s_info_cache[r], buf);
        tft.fillRect(INFO_X_VALUE, y, 320 - INFO_X_VALUE - 4, CHAR_H(2), C_BG);
        text_at(INFO_X_VALUE, y, 2, col, buf);
    }
}

static void show_caminfo(void)
{
    page_frame("CAM INFO");
    for (size_t r = 0; r < INFO_ROWS; r++) {
        text_at(INFO_X_LABEL, INFO_Y0 + (int16_t)r * INFO_DY + 4, 1, C_DIM, INFO_LABEL[r]);
    }
    info_values(true);
}

static char s_time_cache[16];

static void time_value(bool force)
{
    char buf[16];
    uptime_text(buf, sizeof(buf));
    if (!force && strcmp(buf, s_time_cache) == 0) {
        return;
    }
    strcpy(s_time_cache, buf);
    tft.fillRect(20, 80, 280, CHAR_H(5), C_BG);
    text_center(buf, 5, 80, C_TEXT);
}

static void show_time(void)
{
    page_frame("TIME");
    text_center("TIME SINCE POWER ON", 1, 56, C_DIM);
    time_value(true);
    text_center("Real time + date need the GPS", 1, 150, C_YELLOW);
}

/* ---------------------------------------------------------------- */
/* Page switching                                                     */
/* ---------------------------------------------------------------- */

static void open_page(page_t p)
{
    s_page = p;
    s_show_frames = (p == PAGE_CAMERA);
    s_on_start_screen = false;
    s_cam_waiting = false;
    ESP_LOGI(TAG, "Page: %s", PAGE_NAME[p]);
    switch (p) {
    case PAGE_MENU:     show_menu(); break;
    case PAGE_CAMERA:
        if (s_link_up) {
            show_start_screen("Camera found", TFT_LIME);
            s_cam_waiting = true;    /* first picture replaces the screen */
        } else {
            show_start_screen("Searching for camera", TFT_YELLOW);
        }
        break;
    case PAGE_CAMINFO:  show_caminfo(); break;
    case PAGE_TIME:     show_time(); break;
    case PAGE_RIDE:     show_placeholder("RIDE", "Needs the GPS", "speed, trip, ride time"); break;
    case PAGE_LOCATION: show_placeholder("LOCATION", "Needs the GPS", "back to parking, heading, lat / lon"); break;
    case PAGE_SETTINGS: show_placeholder("SETTINGS", "Coming soon", "brightness, link, fps limit"); break;
    default: break;
    }
}

/* ---------------------------------------------------------------- */
/* Boot animation (~3 s)                                              */
/* ---------------------------------------------------------------- */

#define ROAD_Y   178
#define BIKE_Y   166   /* wheel centre line */

static void draw_bike(int16_t x, uint16_t c, bool light)
{
    /* wheels */
    tft.drawCircle(x + 10, BIKE_Y, 10, c);
    tft.drawCircle(x + 10, BIKE_Y, 9, c);
    tft.drawCircle(x + 52, BIKE_Y, 10, c);
    tft.drawCircle(x + 52, BIKE_Y, 9, c);
    /* frame, tank, seat, fork, handlebar */
    tft.drawLine(x + 10, BIKE_Y, x + 26, BIKE_Y - 14, c);
    tft.drawLine(x + 26, BIKE_Y - 14, x + 52, BIKE_Y, c);
    tft.fillRoundRect(x + 24, BIKE_Y - 22, 18, 8, 3, c);         /* tank */
    tft.fillRect(x + 8, BIKE_Y - 20, 16, 4, c);                  /* seat */
    tft.drawLine(x + 46, BIKE_Y - 24, x + 52, BIKE_Y, c);        /* fork */
    tft.drawLine(x + 42, BIKE_Y - 28, x + 48, BIKE_Y - 24, c);   /* handlebar */
    /* headlight */
    tft.fillCircle(x + 56, BIKE_Y - 16, 3, light ? C_YELLOW : c);
}

static void boot_animation(void)
{
    tft.fillScreen(C_BG);
    const int frames = 30;            /* 30 x 100 ms = 3 s */
    int16_t bike_x = -70, last_x = -200;
    for (int f = 0; f < frames; f++) {
        /* road with moving dashes */
        tft.fillRect(0, ROAD_Y, 320, 6, C_BG);
        tft.drawFastHLine(0, ROAD_Y, 320, C_LINE);
        int phase = (f * 9) % 40;
        for (int16_t dx = -phase; dx < 320; dx += 40) {
            tft.fillRect(dx, ROAD_Y + 3, 20, 2, C_DIM);
        }

        /* bike rides in for 14 frames, then stops in the middle */
        if (f < 14) {
            bike_x = -70 + (int16_t)((f + 1) * (130 + 70) / 14);
        }
        if (bike_x != last_x) {
            tft.fillRect(last_x - 2, BIKE_Y - 32, 76, 44, C_BG);
            draw_bike(bike_x, C_TEXT, false);
            last_x = bike_x;
        }
        /* headlight blinks after the stop */
        if (f >= 14 && f < 22) {
            tft.fillCircle(bike_x + 56, BIKE_Y - 16, 3, (f % 2) ? C_YELLOW : C_TEXT);
        }

        /* title fades in: dark grey -> grey -> white/cyan */
        if (f == 15 || f == 18 || f == 21) {
            uint16_t c1 = f == 15 ? C_LINE : (f == 18 ? C_DIM : TFT_WHITE);
            uint16_t c2 = f == 15 ? C_LINE : (f == 18 ? C_DIM : TFT_CYAN);
            const uint8_t ts = 4;
            int16_t tx = (320 - 10 * CHAR_W(ts)) / 2;
            text_at(tx, 40, ts, c1, "Bike");
            text_at(tx + 4 * CHAR_W(ts), 40, ts, c2, "Vision");
            if (f == 21) {
                tft.fillRect(tx, 76, 10 * CHAR_W(ts), 3, TFT_CYAN);
                text_center("Smart Bike Dashboard", 2, 90, TFT_LIGHTGREY);
            }
        }

        /* loading bar */
        tft.drawRect(60, 206, 200, 10, C_LINE);
        tft.fillRect(62, 208, (int16_t)(196 * (f + 1) / frames), 6, C_ACCENT);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ---------------------------------------------------------------- */
/* Input + timer                                                      */
/* ---------------------------------------------------------------- */

static void handle_input(const bv_input_event_t *ev)
{
    if (ev->type == BV_IN_TURN) {
        ESP_LOGI(TAG, "Input: turn %s on %s", ev->dir > 0 ? "RIGHT" : "LEFT", PAGE_NAME[s_page]);
    } else {
        ESP_LOGI(TAG, "Input: %s on %s", ev->type == BV_IN_LONG ? "LONG PRESS" : "PRESS", PAGE_NAME[s_page]);
    }

    if (s_page == PAGE_MENU) {
        if (ev->type == BV_IN_TURN) {
            int old = s_sel;
            s_sel = (s_sel + (ev->dir > 0 ? 1 : 5)) % 6;
            draw_tile(old, false);
            draw_tile(s_sel, true);
        } else if (ev->type == BV_IN_PRESS) {
            open_page(TILE_PAGE[s_sel]);
        } else if (ev->type == BV_IN_LONG) {
            /* manual screen fix: hold the knob 2 s on the menu */
            s_tft_resets++;
            ESP_LOGW(TAG, "Screen re-init by hand (hold on MENU) (%lu)", (unsigned long)s_tft_resets);
            tft.reinit();
            tft.setRotation(TFT_ROTATION);
            open_page(PAGE_MENU);
        }
        return;
    }
    if (s_page == PAGE_BOOT) {
        return;
    }
    if (ev->type == BV_IN_LONG) {
        open_page(PAGE_MENU);    /* back to the menu from any page */
    } else if (ev->type == BV_IN_PRESS) {
        if (s_page == PAGE_CAMERA) {
            ESP_LOGI(TAG, "Camera page: short press is kept for the photo (not built yet)");
        } else {
            open_page(PAGE_MENU);   /* info pages: press = back */
        }
    }
}

static void ui_tick(void)
{
    /* received frames per second, over a 2 s window */
    int64_t now = esp_timer_get_time();
    if (now - s_rx_window_t0 >= 2000000) {
        s_rx_fps10 = (uint32_t)((uint64_t)s_rx_window * 10000000ULL / (uint64_t)(now - s_rx_window_t0));
        s_rx_window = 0;
        s_rx_window_t0 = now;
    }
    if (!s_link_up) {
        s_rx_fps10 = 0;
    }

    switch (s_page) {
    case PAGE_MENU:
        top_bar_status();
        draw_tile_value(0, false);
        break;
    case PAGE_CAMERA:
        if (s_on_start_screen) {
            start_blink();
        }
        break;
    case PAGE_CAMINFO:
        top_bar_status();
        info_values(false);
        break;
    case PAGE_TIME:
        top_bar_status();
        time_value(false);
        break;
    case PAGE_RIDE: case PAGE_LOCATION: case PAGE_SETTINGS:
        top_bar_status();
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- */
/* Display task: the only task that touches the TFT                  */
/* ---------------------------------------------------------------- */

static void handle_frame(const display_msg_t *msg, int64_t *last_us)
{
    s_rx_total++;
    s_rx_window++;
    s_sum_rx++;
    /* frames the camera sent that never arrived complete (or were skipped):
       gaps in the frame id. A smaller id = the camera restarted. */
    if (s_last_fid && msg->frame_id > s_last_fid + 1) {
        uint32_t lost = msg->frame_id - s_last_fid - 1;
        s_lost_total += lost;
        s_sum_lost += lost;
    }
    s_last_fid = msg->frame_id;

    if (msg->data == NULL) {            /* counted only (not on the Camera page) */
        *last_us = 0;
        return;
    }
    if (s_page != PAGE_CAMERA) {
        xSemaphoreGive(s_draw_free);    /* copied, but the page changed: free the buffer */
        *last_us = 0;
        return;
    }

    int64_t t0 = esp_timer_get_time();
    bool ok = draw_jpeg(msg->data, msg->len);
    s_on_start_screen = false;          /* camera picture now on screen */
    s_cam_waiting = false;
    int64_t t1 = esp_timer_get_time();
    xSemaphoreGive(s_draw_free);        /* next frame may be copied in */

    uint32_t gap_ms = *last_us ? (uint32_t)((t0 - *last_us) / 1000) : 0;
    *last_us = t0;
    uint32_t draw_ms = (uint32_t)((t1 - t0) / 1000);
    if (ok) {
        s_drawn++;
        s_sum_n++;
        s_sum_draw_ms += draw_ms;
        if (draw_ms > s_sum_max_draw) s_sum_max_draw = draw_ms;
        if (gap_ms > s_sum_max_gap) s_sum_max_gap = gap_ms;
    } else {
        s_failed++;
        s_sum_fail++;
        ESP_LOGW(TAG, "Frame %lu: %u B, draw FAILED (failed %lu)",
                 (unsigned long)msg->frame_id, (unsigned)msg->len, (unsigned long)s_failed);
    }
    if (BV_FRAME_LOG) ESP_LOGI(TAG, "Frame %lu: %u B, draw %s %lu ms, %lu ms since last, "
                      "waited %lu ms, free heap %u (drawn %lu, failed %lu)",
                 (unsigned long)msg->frame_id, (unsigned)msg->len,
                 ok ? "OK" : "FAILED", (unsigned long)draw_ms, (unsigned long)gap_ms,
                 (unsigned long)msg->wait_ms,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned long)s_drawn, (unsigned long)s_failed);
}

/* One summary line every 10 s on every page:
   received = frames that arrived complete, lost = frames that didn't
   (frame-id gaps), drawn = frames shown (Camera page only). */
static void summary_tick(int64_t now)
{
    if (now - s_sum_t0 < SUMMARY_PERIOD_US) {
        return;
    }
    uint32_t dt_ms = (uint32_t)((now - s_sum_t0) / 1000);
    uint32_t rx10 = dt_ms ? (uint32_t)((uint64_t)s_sum_rx * 10000 / dt_ms) : 0;
    uint32_t dr10 = dt_ms ? (uint32_t)((uint64_t)s_sum_n * 10000 / dt_ms) : 0;
    ESP_LOGI(TAG, "Summary %lu ms on %s: received %lu (%lu.%lu fps), lost %lu, drawn %lu (%lu.%lu fps), "
                  "draw avg %lu / max %lu ms, max gap %lu ms, failed %lu, TFT resets %lu, link %s, free heap %u",
             (unsigned long)dt_ms, PAGE_NAME[s_page],
             (unsigned long)s_sum_rx, (unsigned long)(rx10 / 10), (unsigned long)(rx10 % 10),
             (unsigned long)s_sum_lost,
             (unsigned long)s_sum_n, (unsigned long)(dr10 / 10), (unsigned long)(dr10 % 10),
             (unsigned long)(s_sum_n ? s_sum_draw_ms / s_sum_n : 0),
             (unsigned long)s_sum_max_draw, (unsigned long)s_sum_max_gap,
             (unsigned long)s_sum_fail, (unsigned long)s_tft_resets,
             s_link_up ? "UP" : "DOWN",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    s_sum_t0 = now;
    s_sum_rx = s_sum_lost = s_sum_n = s_sum_draw_ms = s_sum_max_draw = s_sum_max_gap = s_sum_fail = 0;
}

/* ILI9341 "read display power mode" (0x0A): healthy = sleep out (0x10) +
   display on (0x04), normally 0x9C. After a reset by itself it reads 0x08. */
static bool tft_ok(uint8_t *mode)
{
    /* readcommand8 only slows the clock to 24 MHz, too fast for reading the
       ILI9341 (reads gave 0xCE). Read at 4 MHz, then back to full speed. */
    tft.setFrequency(TFT_READ_HZ);
    *mode = tft.readcommand8(0x0A);
    tft.setFrequency(TFT_SPI_HZ);
    return (*mode & 0x14) == 0x14;
}

static void tft_watch_init(void)
{
#if BV_TFT_WATCH
    uint8_t m1, m2;
    bool ok1 = tft_ok(&m1), ok2 = tft_ok(&m2);
    if (ok1 && ok2) {
        s_tft_watch = true;
        ESP_LOGI(TAG, "TFT self-check on (power mode 0x%02X, checked every 2 s)", m1);
    } else {
        ESP_LOGW(TAG, "TFT self-check off: power mode reads 0x%02X / 0x%02X "
                      "(MISO GPIO19 not connected?)", m1, m2);
    }
#endif
}

static void tft_watch(void)
{
    uint8_t m;
    if (!s_tft_watch || tft_ok(&m)) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(20));          /* read once more: no false alarm */
    if (tft_ok(&m)) {
        return;
    }
    s_tft_resets++;
    ESP_LOGW(TAG, "TFT reset itself (power mode 0x%02X) - setting it up again (%lu)",
             m, (unsigned long)s_tft_resets);
    tft.reinit();
    tft.setRotation(TFT_ROTATION);
    if (s_page == PAGE_CAMERA && s_link_up) {
        tft.fillScreen(TFT_BLACK);          /* the next camera picture fills it */
    } else {
        open_page(s_page);
    }
}

static void display_task(void *arg)
{
    int64_t last_us = 0;
    display_msg_t msg;

#if BV_BOOT_ANIM
    boot_animation();
#endif
#if BV_INPUT_TEST
    open_page(PAGE_CAMERA);            /* encoder test: just the live picture */
#else
    open_page(PAGE_MENU);
#endif
    int64_t last_tick = esp_timer_get_time();
    int64_t last_check = last_tick;
    s_rx_window_t0 = last_tick;
    s_sum_t0 = last_tick;

    while (true) {
        if (xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(20)) == pdTRUE) {
            switch (msg.type) {
            case MSG_FRAME:
                handle_frame(&msg, &last_us);
                break;
            case MSG_LINK_UP:
                s_link_up = true;
                s_channel = msg.channel;
                ESP_LOGI(TAG, "Camera link UP (channel %u)", msg.channel);
                if (s_page == PAGE_CAMERA) {
                    show_start_screen("Camera found", TFT_LIME);
                    s_cam_waiting = true;
                }
                break;
            case MSG_LINK_DOWN:
                s_link_up = false;
                ESP_LOGW(TAG, "Camera link DOWN");
                if (s_page == PAGE_CAMERA) {
                    show_start_screen("Camera lost - searching", TFT_ORANGE);
                }
                break;
            }
        }

        bv_input_event_t ev;
        while (s_in_q && xQueueReceive(s_in_q, &ev, 0) == pdTRUE) {
            handle_input(&ev);
        }

        int64_t now = esp_timer_get_time();
        if (now - last_tick >= UI_TICK_US) {
            last_tick = now;
            ui_tick();
        }
        if (now - last_check >= TFT_CHECK_US) {
            last_check = now;
            tft_watch();
        }
        summary_tick(now);
    }
}

/* ---------------------------------------------------------------- */
/* Link callbacks (run in link tasks; never touch the TFT)           */
/* ---------------------------------------------------------------- */

static void on_frame(const uint8_t *data, size_t len, uint32_t frame_id)
{
    if (len < 4 || data[0] != 0xFF || data[1] != 0xD8 ||
        data[len - 2] != 0xFF || data[len - 1] != 0xD9) {
        ESP_LOGW(TAG, "Frame %lu: not a complete JPEG, skipped",
                 (unsigned long)frame_id);
        return;
    }

    /* Not on the Camera page: only count the frame. No copy and no waiting,
       so the link task goes straight back to receiving (waiting here while
       the display task draws text made the link drop packets). */
    if (!s_show_frames) {
        display_msg_t cnt = {};
        cnt.type = MSG_FRAME;
        cnt.data = NULL;
        cnt.len = len;
        cnt.frame_id = frame_id;
        xQueueSend(s_queue, &cnt, 0);
        return;
    }

    /* Wait until the previous frame has been drawn (draw buffer free). */
    int64_t w0 = esp_timer_get_time();
    if (xSemaphoreTake(s_draw_free, pdMS_TO_TICKS(DRAW_WAIT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Frame %lu: draw buffer busy for %d ms, skipped",
                 (unsigned long)frame_id, DRAW_WAIT_MS);
        return;
    }
    uint32_t wait_ms = (uint32_t)((esp_timer_get_time() - w0) / 1000);

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
    ESP_LOGI(TAG, "BikeVision display (pages + live camera)");

    /* drawJpegBuffer() logs ~7 info lines per frame; keep only warnings */
    esp_log_level_set("TFT_JPEG", ESP_LOG_WARN);

    tft.begin();                     /* V1.0 default pins */
    tft.setFrequency(TFT_SPI_HZ);
    tft.setRotation(TFT_ROTATION);
    ESP_LOGI(TAG, "TFT %dx%d", tft.width(), tft.height());
    esp_log_level_set("TFT_SPI", ESP_LOG_WARN);   /* readcommand8 logs 5 lines per call */
    tft_watch_init();

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

#if BV_INPUT_TEST
    if (bv_input_test_start() != ESP_OK) {
        ESP_LOGE(TAG, "Encoder input failed to start");
    }
#else
    s_in_q = xQueueCreate(16, sizeof(bv_input_event_t));
    if (!s_in_q || bv_input_start(s_in_q) != ESP_OK) {
        ESP_LOGE(TAG, "Encoder input failed to start - pages cannot be changed");
    }
#endif

    xTaskCreate(display_task, "display", 8192, NULL, 5, NULL);

    ESP_ERROR_CHECK(espnow_link_radio_start(0));

    espnow_link_config_t cfg = {};
    cfg.role = ESPNOW_LINK_RECEIVER;
    cfg.channel = 0;                  /* scan 1..13, follow the camera */
    cfg.max_frame_size = MAX_FRAME_SIZE;
    cfg.on_frame = on_frame;
    cfg.on_state = on_state;
    ESP_ERROR_CHECK(espnow_link_start(&cfg));
}
