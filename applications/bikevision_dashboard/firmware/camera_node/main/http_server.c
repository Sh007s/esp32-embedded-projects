#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_camera.h"
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"

static const char *TAG = "http_server";
static const char *STREAM_TAG = "stream";

static httpd_handle_t server = NULL;        /* port 80: page, photo, AVI, frames */
static httpd_handle_t stream_server = NULL; /* port 81: /stream only */

#define PHOTO_PATH "/sdcard/photo.jpg"
#define FRAME_COUNT 30
#define FRAME_DELAY_MS 100
#define STREAM_BOUNDARY "bikevisionframe"

/*
 * Live stream server settings (Phase 1).
 * Change ONE of these at a time between tests and save the serial log.
 */
#define STREAM_PORT                81
#define STREAM_CTRL_PORT           32769 /* main server uses default 32768 */
#define STREAM_SEND_WAIT_TIMEOUT_S 5     /* ESP-IDF default; EAGAIN = this expired */
#define STREAM_FRAME_DELAY_MS      1     /* same pacing as before */
#define STREAM_STATS_EVERY         50    /* log measured fps every N frames */
#define PLAYBACK_DELAY_MS 100
#define AVI_PATH "/sdcard/record.avi"
#define AVI_FPS 10
#define AVI_MAX_SECONDS 15
#define AVI_MAX_FRAMES (AVI_FPS * AVI_MAX_SECONDS)
#define AVI_INDEX_ENTRY_SIZE 16

typedef struct {
    uint32_t chunk_offset;
    uint32_t chunk_size;
} avi_index_entry_t;

/* One recording request at a time; keep index outside the HTTP task stack. */
static avi_index_entry_t avi_index[AVI_MAX_FRAMES];

/*
 * Web page
 */
static esp_err_t index_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta name=\"viewport\" "
        "content=\"width=device-width,initial-scale=1\">"
        "<title>BikeVision Camera</title>"
        "</head>"

        "<body>"
        "<h1>BikeVision Camera</h1>"
        "<p style=\"color:gray;font-size:small\">Page version: 2 (AVI playback)</p>"

        "<p>"
        "<button onclick=\"capturePhoto()\">"
        "Capture New Photo"
        "</button>"
        "</p>"

        "<p>"
        "<img id=\"photo\" "
        "src=\"/photo?t=0\" "
        "style=\"max-width:100%;height:auto;\">"
        "</p>"

        "<p>"
        "<button onclick=\"refreshPhoto()\">"
        "Refresh Photo"
        "</button>"
        "</p>"

        "<h2>Recorded Video</h2>"
        "<p>Record a real AVI video file to the SD card, then play or download it.</p>"
        "<button onclick=\"recordVideo()\">Record 10-Second Video</button> "
        "<a href=\"/record.avi\" download=\"record.avi\">Download Recorded Video</a> "
        "<a href=\"/record_status\" target=\"_blank\">Check SD Video File</a>"
        /* Browsers can't play MJPEG-AVI in <video>, so the recorded frames
         * are played back from the ESP32 as an MJPEG stream into an <img>. */
        "<p><button onclick=\"playRecordedVideo()\">Play Recorded Video</button></p>"
        "<p><img id=\"recordedVideo\" alt=\"Recorded video playback\" "
        "style=\"max-width:100%;height:auto;\"></p>"

        "<h2>Live Video</h2>"
        "<p><button onclick=\"startLiveVideo()\">Start Live Video</button> "
        "<button onclick=\"stopLiveVideo()\">Stop Live Video</button></p>"
        "<p><img id=\"liveStream\" alt=\"Live camera stream\" "
        "style=\"max-width:100%;height:auto;\"></p>"

        "<p>"
        "<a href=\"/download\">"
        "<button>Download Photo</button>"
        "</a>"
        "</p>"

        "<p id=\"status\"></p>"

        "<script>"

        "let recordingInProgress = false;"

        "function refreshPhoto() {"
        "    document.getElementById('photo').src = "
        "        '/photo?t=' + new Date().getTime();"
        "}"

        "function recordVideo() {"
        "    if (recordingInProgress) return;"
        "    recordingInProgress = true;"
        "    stopLiveVideo();"
        "    const playback = document.getElementById('recordedVideo');"
        "    playback.onerror = null;"
        "    playback.removeAttribute('src');"
        "    const status = document.getElementById('status');"
        "    status.innerText = 'Recording AVI video for up to 10 seconds...';"
        "    fetch('/record_video?seconds=10')"
        "      .then(async function(response) {"
        "        const message = await response.text();"
        "        if (!response.ok) throw new Error(message);"
        "        return message;"
        "      })"
        "      .then(function(message) {"
        "        status.innerText = message + ' Playing recorded video...';"
        "        playRecordedVideo();"
        "      })"
        "      .catch(function(error) { status.innerText = 'Recording failed: ' + error.message; })"
        "      .finally(function() { recordingInProgress = false; });"
        "}"

        /* Playback shares the port-81 server with live video, so live video
         * is stopped first; otherwise playback would wait for it to end. */
        "function playRecordedVideo() {"
        "    stopLiveVideo();"
        "    const view = document.getElementById('recordedVideo');"
        "    view.onerror = function() {"
        "        document.getElementById('status').innerText = 'No recorded video to play.';"
        "    };"
        "    view.src = 'http://' + location.hostname + ':81/playback?t=' + Date.now();"
        "    document.getElementById('status').innerText = 'Playing recorded video...';"
        "}"

        /* Live stream is served by a separate server on port 81 so it can't
         * block the buttons on this page. */
        "function startLiveVideo() {"
        "    const playback = document.getElementById('recordedVideo');"
        "    playback.onerror = null;"
        "    playback.removeAttribute('src');"
        "    const stream = document.getElementById('liveStream');"
        "    stream.onerror = function() {"
        "        document.getElementById('status').innerText = 'Live video connection lost.';"
        "    };"
        "    stream.src = 'http://' + location.hostname + ':81/stream?t=' + Date.now();"
        "    document.getElementById('status').innerText = 'Live video started.';"
        "}"
        "function stopLiveVideo() {"
        "    const stream = document.getElementById('liveStream');"
        "    stream.onerror = null;"
        "    stream.removeAttribute('src');"
        "    document.getElementById('status').innerText = 'Live video stopped.';"
        "}"

        "function capturePhoto() {"
        "    document.getElementById('status').innerText = "
        "        'Capturing new photo...';"

        "    fetch('/capture')"
        "        .then(function(response) {"
        "            if (!response.ok) {"
        "                throw new Error('Capture failed');"
        "            }"

        "            return response.text();"
        "        })"
        "        .then(function() {"
        "            document.getElementById('status').innerText = "
        "                'New photo captured';"

        "            document.getElementById('photo').src = "
        "                '/photo?t=' + new Date().getTime();"
        "        })"
        "        .catch(function(error) {"
        "            document.getElementById('status').innerText = "
        "                'Capture failed';"

        "            console.error(error);"
        "        });"
        "}"

        "</script>"

        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");

    httpd_resp_set_hdr(
        req,
        "Cache-Control",
        "no-store");

    return httpd_resp_send(
        req,
        html,
        HTTPD_RESP_USE_STRLEN);
}

/*
 * Send existing JPEG from SD card
 */
static esp_err_t photo_handler(httpd_req_t *req)
{
    FILE *file = fopen(PHOTO_PATH, "rb");

    if (file == NULL)
    {
        ESP_LOGE(TAG,
                 "Failed to open %s",
                 PHOTO_PATH);

        httpd_resp_send_404(req);

        return ESP_FAIL;
    }

    httpd_resp_set_type(
        req,
        "image/jpeg");

    /*
     * Prevent browser caching.
     */
    httpd_resp_set_hdr(
        req,
        "Cache-Control",
        "no-store, no-cache, must-revalidate");

    httpd_resp_set_hdr(
        req,
        "Pragma",
        "no-cache");

    httpd_resp_set_hdr(
        req,
        "Expires",
        "0");

    /*
     * Keep buffer small because this handler
     * runs inside the HTTPD task.
     */
    char buffer[1024];

    size_t bytes_read;

    while ((bytes_read =
                fread(
                    buffer,
                    1,
                    sizeof(buffer),
                    file)) > 0)
    {
        if (httpd_resp_send_chunk(
                req,
                buffer,
                bytes_read) != ESP_OK)
        {
            fclose(file);

            return ESP_FAIL;
        }
    }

    fclose(file);

    /*
     * End chunked response.
     */
    httpd_resp_send_chunk(
        req,
        NULL,
        0);

    ESP_LOGI(
        TAG,
        "Sent %s",
        PHOTO_PATH);

    return ESP_OK;
}

/*
 * Download existing JPEG
 */
static esp_err_t download_handler(httpd_req_t *req)
{
    FILE *file = fopen(PHOTO_PATH, "rb");

    if (file == NULL)
    {
        ESP_LOGE(TAG,
                 "Failed to open %s",
                 PHOTO_PATH);

        httpd_resp_send_404(req);

        return ESP_FAIL;
    }

    httpd_resp_set_type(
        req,
        "image/jpeg");

    httpd_resp_set_hdr(
        req,
        "Content-Disposition",
        "attachment; filename=photo.jpg");

    /*
     * Keep buffer small because this handler
     * runs inside the HTTPD task.
     */
    char buffer[1024];

    size_t bytes_read;

    while ((bytes_read =
                fread(
                    buffer,
                    1,
                    sizeof(buffer),
                    file)) > 0)
    {
        if (httpd_resp_send_chunk(
                req,
                buffer,
                bytes_read) != ESP_OK)
        {
            fclose(file);

            return ESP_FAIL;
        }
    }

    fclose(file);

    /*
     * End chunked response.
     */
    httpd_resp_send_chunk(
        req,
        NULL,
        0);

    return ESP_OK;
}

/*
 * Capture a new JPEG and save it to SD card.
 */
static esp_err_t capture_handler(httpd_req_t *req)
{
    ESP_LOGI(
        TAG,
        "Capturing new photo...");

    /*
     * With fb_count = 1 the camera keeps one frame waiting in its buffer.
     * After an idle period that waiting frame can be old, so take it and
     * throw it away; the next esp_camera_fb_get() returns a fresh frame.
     */
    camera_fb_t *stale = esp_camera_fb_get();
    if (stale != NULL)
    {
        esp_camera_fb_return(stale);
    }

    camera_fb_t *fb =
        esp_camera_fb_get();

    if (fb == NULL)
    {
        ESP_LOGE(
            TAG,
            "Camera capture failed");

        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    if (fb->format != PIXFORMAT_JPEG)
    {
        ESP_LOGE(
            TAG,
            "Camera framebuffer is not JPEG");

        esp_camera_fb_return(fb);

        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    FILE *file =
        fopen(
            PHOTO_PATH,
            "wb");

    if (file == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to open %s",
            PHOTO_PATH);

        esp_camera_fb_return(fb);

        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    size_t image_size =
        fb->len;

    size_t written =
        fwrite(
            fb->buf,
            1,
            fb->len,
            file);

    fclose(file);

    /*
     * Return camera frame buffer.
     */
    esp_camera_fb_return(fb);

    if (written != image_size)
    {
        ESP_LOGE(
            TAG,
            "Failed to save complete JPEG");

        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "New photo saved: %u bytes",
        (unsigned)image_size);

    /*
     * No redirect.
     *
     * The browser JavaScript will request
     * /photo with a unique timestamp after
     * this request completes.
     */
    httpd_resp_set_type(
        req,
        "text/plain");

    return httpd_resp_send(
        req,
        "OK",
        2);
}

/*
 * Capture multiple JPEG frames without intentionally overwriting existing files.
 * Filenames are frame_000.jpg, frame_001.jpg, etc.
 */

/* Little-endian AVI header helpers. The file contains Motion-JPEG (MJPG) frames. */
static bool avi_write_u16(FILE *f, uint16_t v)
{
    return fputc(v & 0xff, f) != EOF && fputc((v >> 8) & 0xff, f) != EOF;
}

static bool avi_write_u32(FILE *f, uint32_t v)
{
    return fputc(v & 0xff, f) != EOF &&
           fputc((v >> 8) & 0xff, f) != EOF &&
           fputc((v >> 16) & 0xff, f) != EOF &&
           fputc((v >> 24) & 0xff, f) != EOF;
}

static bool avi_write_fourcc(FILE *f, const char id[4])
{
    return fwrite(id, 1, 4, f) == 4;
}

static bool avi_patch_u32(FILE *f, long offset, uint32_t value)
{
    if (fseek(f, offset, SEEK_SET) != 0 || !avi_write_u32(f, value)) return false;
    return true;
}

static bool avi_write_header(FILE *f, uint16_t width, uint16_t height)
{
    /* RIFF AVI / LIST hdrl / avih */
    if (!avi_write_fourcc(f, "RIFF") || !avi_write_u32(f, 0) ||
        !avi_write_fourcc(f, "AVI ") || !avi_write_fourcc(f, "LIST") ||
        !avi_write_u32(f, 192) || !avi_write_fourcc(f, "hdrl") ||
        !avi_write_fourcc(f, "avih") || !avi_write_u32(f, 56)) return false;
    /* avih: 14 DWORDs = 56 bytes. Order: us/frame, max bytes/s, padding,
     * flags, total frames, initial frames, streams, suggested buffer,
     * width, height, reserved[4]. (Previously one extra DWORD was written
     * here, shifting every later field by 4 bytes.) */
    if (!avi_write_u32(f, 1000000U / AVI_FPS) || !avi_write_u32(f, 0) ||
        !avi_write_u32(f, 0) || !avi_write_u32(f, 0x10) ||
        !avi_write_u32(f, 0) || !avi_write_u32(f, 0) ||
        !avi_write_u32(f, 1) || !avi_write_u32(f, 0) ||
        !avi_write_u32(f, width) ||
        !avi_write_u32(f, height)) return false;
    for (int i = 0; i < 4; i++) if (!avi_write_u32(f, 0)) return false;

    /* LIST strl / strh */
    if (!avi_write_fourcc(f, "LIST") || !avi_write_u32(f, 116) ||
        !avi_write_fourcc(f, "strl") || !avi_write_fourcc(f, "strh") ||
        !avi_write_u32(f, 56) || !avi_write_fourcc(f, "vids") ||
        !avi_write_fourcc(f, "MJPG") || !avi_write_u32(f, 0) ||
        !avi_write_u16(f, 0) || !avi_write_u16(f, 0) ||
        !avi_write_u32(f, 0) || !avi_write_u32(f, 1) ||
        !avi_write_u32(f, AVI_FPS) || !avi_write_u32(f, 0) ||
        /* length (patched later), suggested buffer size, quality.
         * (Suggested buffer size was previously missing, which shifted
         * quality/sample size/frame rect by 4 bytes.) */
        !avi_write_u32(f, 0) || !avi_write_u32(f, 0) ||
        !avi_write_u32(f, 0xFFFFFFFFU) ||
        !avi_write_u32(f, 0) || !avi_write_u16(f, 0) ||
        !avi_write_u16(f, 0) || !avi_write_u16(f, width) ||
        !avi_write_u16(f, height)) return false;

    /* BITMAPINFOHEADER */
    if (!avi_write_fourcc(f, "strf") || !avi_write_u32(f, 40) ||
        !avi_write_u32(f, 40) || !avi_write_u32(f, width) ||
        !avi_write_u32(f, height) || !avi_write_u16(f, 1) ||
        !avi_write_u16(f, 24) || !avi_write_fourcc(f, "MJPG") ||
        !avi_write_u32(f, 0) || !avi_write_u32(f, 0) ||
        !avi_write_u32(f, 0) || !avi_write_u32(f, 0) ||
        !avi_write_u32(f, 0)) return false;

    return avi_write_fourcc(f, "LIST") && avi_write_u32(f, 0) &&
           avi_write_fourcc(f, "movi");
}

static esp_err_t record_video_handler(httpd_req_t *req)
{
    char query[32], value[8];
    int seconds = 10;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "seconds", value, sizeof(value)) == ESP_OK)
    {
        char *end = NULL;
        long parsed = strtol(value, &end, 10);
        if (end == value || *end != '\0' || parsed < 1 || parsed > AVI_MAX_SECONDS)
        {
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_send(req, "seconds must be 1..15", HTTPD_RESP_USE_STRLEN);
        }
        seconds = (int)parsed;
    }

    FILE *file = fopen(AVI_PATH, "wb");
    if (!file)
    {
        ESP_LOGE(TAG, "Cannot create %s: errno=%d", AVI_PATH, errno);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    camera_fb_t *first = esp_camera_fb_get();
    if (!first || first->format != PIXFORMAT_JPEG || !first->buf || !first->len)
    {
        if (first) esp_camera_fb_return(first);
        fclose(file);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    uint16_t width = (uint16_t)first->width;
    uint16_t height = (uint16_t)first->height;
    if (!avi_write_header(file, width, height))
    {
        esp_camera_fb_return(first); fclose(file); httpd_resp_send_500(req); return ESP_FAIL;
    }

    int target_frames = seconds * AVI_FPS;
    int frame_count = 0;
    bool failed = false;
    int64_t record_start_us = esp_timer_get_time();
    for (int i = 0; i < target_frames; i++)
    {
        camera_fb_t *fb = (i == 0) ? first : esp_camera_fb_get();
        if (!fb || fb->format != PIXFORMAT_JPEG || !fb->buf || !fb->len)
        {
            if (fb) esp_camera_fb_return(fb);
            failed = true;
            break;
        }
        if (fb->width != width || fb->height != height || fb->len > UINT32_MAX)
        {
            esp_camera_fb_return(fb); failed = true; break;
        }
        long chunk_pos = ftell(file);
        if (chunk_pos < 0 || !avi_write_fourcc(file, "00dc") ||
            !avi_write_u32(file, (uint32_t)fb->len) ||
            fwrite(fb->buf, 1, fb->len, file) != fb->len ||
            ((fb->len & 1U) && fputc(0, file) == EOF))
        {
            esp_camera_fb_return(fb); failed = true; break;
        }
        avi_index[frame_count].chunk_offset = (uint32_t)(chunk_pos - 220);
        avi_index[frame_count].chunk_size = (uint32_t)fb->len;
        frame_count++;
        esp_camera_fb_return(fb);

        /* Wait only for the time left until the next frame slot, instead of a
         * fixed 100 ms after every frame (capture + SD write already take time). */
        int64_t next_frame_us = record_start_us +
                                (int64_t)(i + 1) * (1000000 / AVI_FPS);
        int64_t wait_us = next_frame_us - esp_timer_get_time();
        if (wait_us > 1000)
        {
            vTaskDelay(pdMS_TO_TICKS(wait_us / 1000));
        }
    }

    if (frame_count == 0) failed = true;
    long movi_end = ftell(file);
    if (movi_end < 0) failed = true;

    /* Real time per frame, so playback runs at the speed it was recorded. */
    uint32_t us_per_frame = 1000000U / AVI_FPS;
    if (frame_count > 0)
    {
        int64_t elapsed_us = esp_timer_get_time() - record_start_us;
        if (elapsed_us > 0)
        {
            us_per_frame = (uint32_t)(elapsed_us / frame_count);
        }
    }

    if (!failed)
    {
        uint32_t movi_size = (uint32_t)(movi_end - 220);
        /* Patch the movi size, then seek BACK to the end of the frames
         * before writing idx1. (Previously idx1 was written at offset 220,
         * overwriting the 'movi' header and the first frames.) */
        if (!avi_patch_u32(file, 216, movi_size) ||
            fseek(file, movi_end, SEEK_SET) != 0 ||
            !avi_write_fourcc(file, "idx1") ||
            !avi_write_u32(file, (uint32_t)frame_count * AVI_INDEX_ENTRY_SIZE)) failed = true;
        for (int i = 0; !failed && i < frame_count; i++)
        {
            if (!avi_write_fourcc(file, "00dc") || !avi_write_u32(file, 0x10) ||
                !avi_write_u32(file, avi_index[i].chunk_offset) ||
                !avi_write_u32(file, avi_index[i].chunk_size)) failed = true;
        }
        long end_pos = ftell(file);
        if (end_pos < 0 || !avi_patch_u32(file, 4, (uint32_t)(end_pos - 8)) ||
            !avi_patch_u32(file, 32, us_per_frame) ||          /* avih us/frame */
            !avi_patch_u32(file, 48, (uint32_t)frame_count) || /* avih total frames */
            !avi_patch_u32(file, 128, us_per_frame) ||         /* strh scale */
            !avi_patch_u32(file, 132, 1000000U) ||             /* strh rate */
            !avi_patch_u32(file, 140, (uint32_t)frame_count))  /* strh length */
            failed = true;
    }
    if (fclose(file) != 0) failed = true;
    if (failed)
    {
        ESP_LOGE(TAG, "AVI recording failed after %d frames", frame_count);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Recorded AVI: %d frames, %.1f fps actual, to %s",
             frame_count, 1000000.0f / (float)us_per_frame, AVI_PATH);
    httpd_resp_set_type(req, "text/plain");
    char message[96];
    snprintf(message, sizeof(message), "Recorded %d frames in record.avi (MJPEG AVI)", frame_count);
    return httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
}

/* Read-only online diagnostic for the AVI file on the SD card. */
static esp_err_t recording_status_handler(httpd_req_t *req)
{
    struct stat st;
    if (stat(AVI_PATH, &st) != 0) {
        char response[128];
        snprintf(response, sizeof(response),
                 "exists=false\npath=%s\nstat_errno=%d\n",
                 AVI_PATH, errno);
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    FILE *file = fopen(AVI_PATH, "rb");
    if (file == NULL) {
        char response[160];
        snprintf(response, sizeof(response),
                 "exists=true\nsize_bytes=%lld\nreadable=false\nfopen_errno=%d\n",
                 (long long)st.st_size, errno);
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    char buffer[2048];
    uint64_t bytes_read = 0;
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        bytes_read += n;
    }
    bool read_ok = (ferror(file) == 0);
    int close_result = fclose(file);
    if (close_result != 0) {
        read_ok = false;
    }

    char response[192];
    snprintf(response, sizeof(response),
             "exists=true\nsize_bytes=%lld\nbytes_read=%llu\nreadable=%s\nsize_matches=%s\n",
             (long long)st.st_size,
             (unsigned long long)bytes_read,
             read_ok ? "true" : "false",
             ((uint64_t)st.st_size == bytes_read) ? "true" : "false");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    ESP_LOGI(TAG, "AVI status: exists=true size=%lld bytes_read=%llu readable=%s",
             (long long)st.st_size,
             (unsigned long long)bytes_read,
             read_ok ? "true" : "false");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t recording_download_handler(httpd_req_t *req)
{
    FILE *file = fopen(AVI_PATH, "rb");
    if (!file) { httpd_resp_send_404(req); return ESP_FAIL; }
    httpd_resp_set_type(req, "video/x-msvideo");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=record.avi");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char buffer[2048];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        if (httpd_resp_send_chunk(req, buffer, n) != ESP_OK) { fclose(file); return ESP_FAIL; }
    }
    fclose(file);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t capture_frames_handler(httpd_req_t *req)
{
    int saved_count = 0;
    int failed_count = 0;
    int saved_numbers[FRAME_COUNT];

    ESP_LOGI(TAG, "Starting %d-frame capture", FRAME_COUNT);

    for (int i = 0; i < FRAME_COUNT; i++)
    {
        camera_fb_t *fb = esp_camera_fb_get();

        if (fb == NULL)
        {
            ESP_LOGE(TAG, "Frame %d capture failed", i);
            failed_count++;
            vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
            continue;
        }

        if (fb->format != PIXFORMAT_JPEG)
        {
            ESP_LOGE(TAG, "Frame %d is not JPEG", i);
            esp_camera_fb_return(fb);
            failed_count++;
            vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
            continue;
        }

        char filename[48];
        int frame_number = i;
        FILE *frame_file = NULL;

        /*
         * Create a new file without overwriting an existing one.
         * If the name already exists, try the next number.
         */
        while (frame_number >= 0)
        {
            snprintf(filename, sizeof(filename),
                     "/sdcard/frame%03d.jpg", frame_number);

            errno = 0;
            frame_file = fopen(filename, "wx");

            if (frame_file != NULL)
            {
                break;
            }

            int err = errno;

            if (err == EEXIST)
            {
                if (frame_number == INT_MAX)
                {
                    ESP_LOGE(TAG, "Frame filename counter exhausted");
                    break;
                }

                frame_number++;
                continue;
            }

            ESP_LOGE(TAG,
                     "Cannot create %s: errno=%d (%s)",
                     filename, err, strerror(err));
            break;
        }

        if (frame_file == NULL)
        {
            failed_count++;
        }
        else
        {
            size_t expected = fb->len;
            size_t written = fwrite(fb->buf, 1, expected, frame_file);
            int write_error = ferror(frame_file);
            int close_result = fclose(frame_file);

            if (written == expected &&
                write_error == 0 &&
                close_result == 0)
            {
                ESP_LOGI(TAG, "Saved %s (%u bytes)",
                         filename, (unsigned int)written);
                saved_numbers[saved_count++] = frame_number;
            }
            else
            {
                ESP_LOGE(TAG,
                         "Incomplete write: %s, written=%u/%u, "
                         "ferror=%d, fclose=%d",
                         filename,
                         (unsigned int)written,
                         (unsigned int)expected,
                         write_error,
                         close_result);

                failed_count++;
            }
        }

        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
    }

    char response[512];
    size_t used = (size_t)snprintf(response, sizeof(response),
                                   "{\"saved\":%d,\"failed\":%d,\"frames\":[",
                                   saved_count, failed_count);

    for (int i = 0; i < saved_count && used < sizeof(response); i++)
    {
        int n = snprintf(response + used, sizeof(response) - used,
                         "%s%d", (i == 0) ? "" : ",", saved_numbers[i]);
        if (n < 0 || (size_t)n >= sizeof(response) - used)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        used += (size_t)n;
    }

    if (used >= sizeof(response) ||
        snprintf(response + used, sizeof(response) - used, "]}") >=
            (int)(sizeof(response) - used))
    {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t frame_handler(httpd_req_t *req)
{
    char query[32];
    char value[12];
    char filename[48];
    char *end = NULL;
    long parsed_number;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "num", value, sizeof(value)) != ESP_OK)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    parsed_number = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed_number < 0 ||
        parsed_number >= 10000)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    int frame_number = (int)parsed_number;

    if (frame_number < 0 || frame_number >= 10000)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    snprintf(filename, sizeof(filename),
             "/sdcard/frame%03d.jpg", frame_number);

    FILE *file = fopen(filename, "rb");
    if (file == NULL)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char buffer[1024];
    size_t n;

    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        if (httpd_resp_send_chunk(req, buffer, n) != ESP_OK)
        {
            fclose(file);
            return ESP_FAIL;
        }
    }

    fclose(file);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* Current Wi-Fi signal strength in dBm, or 0 if not available. */
static int stream_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    {
        return ap.rssi;
    }
    return 0;
}

/*
 * Send one piece of a frame. On failure, log which stage failed and how long
 * the send blocked:
 *   blocked ~= STREAM_SEND_WAIT_TIMEOUT_S -> socket timed out (Wi-Fi or client
 *                                            not keeping up) = real problem
 *   blocked ~= 0 ms                       -> client had already closed
 *                                            (Stop pressed / tab closed) = normal
 */
static esp_err_t stream_send_stage(httpd_req_t *req, const char *stage,
                                   const char *buf, size_t len,
                                   uint32_t frame_no)
{
    int64_t t0 = esp_timer_get_time();
    esp_err_t res = httpd_resp_send_chunk(req, buf, len);

    if (res != ESP_OK)
    {
        ESP_LOGW(STREAM_TAG,
                 "send failed: stage=%s frame=%lu len=%u blocked=%lldms "
                 "rssi=%d heap=%u psram=%u",
                 stage, (unsigned long)frame_no, (unsigned)len,
                 (long long)((esp_timer_get_time() - t0) / 1000),
                 stream_rssi(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
    return res;
}

/*
 * Stream camera JPEG frames to a browser as an MJPEG multipart response.
 * Runs on its own server (port 81), so a long-running stream no longer
 * blocks /photo, /capture or the web page on port 80.
 * The browser disconnects this handler when Stop Live Video clears the image.
 */
static esp_err_t stream_handler(httpd_req_t *req)
{
    uint32_t frames = 0;
    size_t window_bytes = 0;
    int64_t t_start = esp_timer_get_time();
    int64_t t_window = t_start;
    esp_err_t result = ESP_OK;

    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    ESP_LOGI(STREAM_TAG, "client connected: fd=%d rssi=%d",
             httpd_req_to_sockfd(req), stream_rssi());

    while (1)
    {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL)
        {
            ESP_LOGE(STREAM_TAG, "camera capture failed at frame=%lu",
                     (unsigned long)frames);
            result = ESP_FAIL;
            break;
        }

        if (fb->format != PIXFORMAT_JPEG || fb->buf == NULL || fb->len == 0)
        {
            esp_camera_fb_return(fb);
            ESP_LOGE(STREAM_TAG, "invalid/non-JPEG frame at frame=%lu",
                     (unsigned long)frames);
            result = ESP_FAIL;
            break;
        }

        char part_header[128];
        int header_len = snprintf(part_header, sizeof(part_header),
                                  "--" STREAM_BOUNDARY "\r\n"
                                  "Content-Type: image/jpeg\r\n"
                                  "Content-Length: %u\r\n\r\n",
                                  (unsigned int)fb->len);
        size_t frame_len = fb->len;

        if (header_len < 0 || (size_t)header_len >= sizeof(part_header))
        {
            result = ESP_FAIL;
        }
        else
        {
            result = stream_send_stage(req, "part_header",
                                       part_header, header_len, frames);
            if (result == ESP_OK)
            {
                result = stream_send_stage(req, "jpeg_payload",
                                           (const char *)fb->buf, fb->len, frames);
            }
            if (result == ESP_OK)
            {
                result = stream_send_stage(req, "trailing_crlf", "\r\n", 2, frames);
            }
        }

        /* Always return the frame buffer, on success and on failure. */
        esp_camera_fb_return(fb);

        /* After a failed send the multipart stream is out of sync:
         * stop here and let httpd close the socket. */
        if (result != ESP_OK)
        {
            break;
        }

        frames++;
        window_bytes += frame_len;

        if (frames % STREAM_STATS_EVERY == 0)
        {
            int64_t now = esp_timer_get_time();
            float secs = (float)(now - t_window) / 1000000.0f;
            ESP_LOGI(STREAM_TAG,
                     "frames=%lu measured_fps=%.1f avg_kB=%.1f rssi=%d heap=%u psram=%u",
                     (unsigned long)frames,
                     secs > 0 ? (float)STREAM_STATS_EVERY / secs : 0.0f,
                     (float)window_bytes / 1024.0f / STREAM_STATS_EVERY,
                     stream_rssi(),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            t_window = now;
            window_bytes = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(STREAM_FRAME_DELAY_MS));
    }

    ESP_LOGI(STREAM_TAG, "stream ended: frames=%lu duration=%llds result=%s",
             (unsigned long)frames,
             (long long)((esp_timer_get_time() - t_start) / 1000000),
             esp_err_to_name(result));
    return result;
}

/*
 * Recorded-video playback.
 *
 * Browsers can't play MJPEG-AVI in a <video> element, so this reads
 * record.avi from the SD card and sends its JPEG frames as the same kind of
 * MJPEG multipart stream as /stream, paced at the recorded frame rate.
 * Read-only: the AVI recording code is not changed.
 */
#define PLAYBACK_MAX_FRAME_BYTES (256 * 1024) /* reject corrupt sizes */

static bool avi_read_u32(FILE *f, uint32_t *out)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4)
    {
        return false;
    }
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

static bool avi_read_fourcc(FILE *f, char out[4])
{
    return fread(out, 1, 4, f) == 4;
}

/*
 * Find the 'movi' list. On success *movi_data is the file offset of the
 * first chunk inside it and *movi_end is the offset just past the list.
 * Also returns the frame interval from the main AVI header.
 */
static bool avi_find_movi(FILE *f, long *movi_data, long *movi_end,
                          uint32_t *frame_us)
{
    char id[4];
    uint32_t size;

    if (fseek(f, 0, SEEK_SET) != 0 ||
        !avi_read_fourcc(f, id) || memcmp(id, "RIFF", 4) != 0 ||
        !avi_read_u32(f, &size) ||
        !avi_read_fourcc(f, id) || memcmp(id, "AVI ", 4) != 0)
    {
        return false;
    }

    *frame_us = 0;

    for (int guard = 0; guard < 64; guard++)
    {
        long chunk_pos = ftell(f);
        if (chunk_pos < 0 || !avi_read_fourcc(f, id) || !avi_read_u32(f, &size))
        {
            return false;
        }

        if (memcmp(id, "LIST", 4) == 0)
        {
            char list_type[4];
            if (!avi_read_fourcc(f, list_type))
            {
                return false;
            }
            if (memcmp(list_type, "movi", 4) == 0)
            {
                *movi_data = chunk_pos + 12;
                *movi_end = chunk_pos + 8 + (long)size;
                return true;
            }
            if (memcmp(list_type, "hdrl", 4) == 0)
            {
                /* 'avih' chunk follows; its first field is microseconds per frame. */
                char avih[4];
                uint32_t avih_size, us;
                long hdrl_body = ftell(f);
                if (avi_read_fourcc(f, avih) && memcmp(avih, "avih", 4) == 0 &&
                    avi_read_u32(f, &avih_size) && avi_read_u32(f, &us))
                {
                    *frame_us = us;
                }
                if (hdrl_body < 0 || fseek(f, hdrl_body, SEEK_SET) != 0)
                {
                    return false;
                }
            }
            /* Skip the rest of this list (size includes the 4-byte type). */
            if (fseek(f, chunk_pos + 8 + (long)size + (size & 1), SEEK_SET) != 0)
            {
                return false;
            }
        }
        else
        {
            if (fseek(f, (long)size + (size & 1), SEEK_CUR) != 0)
            {
                return false;
            }
        }
    }
    return false;
}

static esp_err_t playback_handler(httpd_req_t *req)
{
    FILE *file = fopen(AVI_PATH, "rb");
    if (file == NULL)
    {
        ESP_LOGW(STREAM_TAG, "playback requested but %s not found", AVI_PATH);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    ESP_LOGI(STREAM_TAG, "playback: client connected fd=%d", httpd_req_to_sockfd(req));

    long pos = 0, movi_end = 0;
    uint32_t frame_us = 0;
    if (!avi_find_movi(file, &pos, &movi_end, &frame_us))
    {
        ESP_LOGE(STREAM_TAG, "playback: %s is not a readable AVI", AVI_PATH);
        fclose(file);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    /* Fall back to the recorder's rate if the header value looks wrong. */
    if (frame_us < 10000 || frame_us > 2000000)
    {
        frame_us = 1000000U / AVI_FPS;
    }

    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    uint8_t *frame = NULL;
    size_t frame_cap = 0;
    uint32_t frames = 0;
    esp_err_t result = ESP_OK;
    int64_t t_start = esp_timer_get_time();

    while (pos + 8 <= movi_end)
    {
        char id[4];
        uint32_t size;
        if (fseek(file, pos, SEEK_SET) != 0 ||
            !avi_read_fourcc(file, id) || !avi_read_u32(file, &size))
        {
            ESP_LOGE(STREAM_TAG, "playback: read error at offset %ld", pos);
            result = ESP_FAIL;
            break;
        }

        long next = pos + 8 + (long)size + (size & 1);
        if (next > movi_end || next <= pos)
        {
            ESP_LOGE(STREAM_TAG, "playback: bad chunk size %lu at offset %ld",
                     (unsigned long)size, pos);
            result = ESP_FAIL;
            break;
        }

        /* Video frames are '00dc'; skip anything else. */
        if (memcmp(id, "00dc", 4) == 0 && size > 0)
        {
            if (size > PLAYBACK_MAX_FRAME_BYTES)
            {
                ESP_LOGE(STREAM_TAG, "playback: frame too large (%lu bytes)",
                         (unsigned long)size);
                result = ESP_FAIL;
                break;
            }
            if (size > frame_cap)
            {
                uint8_t *bigger = heap_caps_realloc(frame, size,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (bigger == NULL)
                {
                    ESP_LOGE(STREAM_TAG, "playback: no memory for %lu bytes",
                             (unsigned long)size);
                    result = ESP_ERR_NO_MEM;
                    break;
                }
                frame = bigger;
                frame_cap = size;
            }
            if (fread(frame, 1, size, file) != size)
            {
                ESP_LOGE(STREAM_TAG, "playback: short read in frame %lu",
                         (unsigned long)frames);
                result = ESP_FAIL;
                break;
            }

            int64_t t_frame = esp_timer_get_time();

            char part_header[128];
            int header_len = snprintf(part_header, sizeof(part_header),
                                      "--" STREAM_BOUNDARY "\r\n"
                                      "Content-Type: image/jpeg\r\n"
                                      "Content-Length: %lu\r\n\r\n",
                                      (unsigned long)size);
            result = stream_send_stage(req, "pb_part_header",
                                       part_header, header_len, frames);
            if (result == ESP_OK)
            {
                result = stream_send_stage(req, "pb_jpeg_payload",
                                           (const char *)frame, size, frames);
            }
            if (result == ESP_OK)
            {
                result = stream_send_stage(req, "pb_trailing_crlf", "\r\n", 2, frames);
            }
            if (result != ESP_OK)
            {
                break;
            }
            frames++;

            /* Pace to the recorded frame rate. */
            int64_t elapsed_us = esp_timer_get_time() - t_frame;
            if (elapsed_us < (int64_t)frame_us)
            {
                vTaskDelay(pdMS_TO_TICKS((frame_us - elapsed_us) / 1000));
            }
        }

        pos = next;
    }

    free(frame);
    fclose(file);

    ESP_LOGI(STREAM_TAG, "playback ended: frames=%lu duration=%llds result=%s",
             (unsigned long)frames,
             (long long)((esp_timer_get_time() - t_start) / 1000000),
             esp_err_to_name(result));

    if (result == ESP_OK)
    {
        /* Finish the multipart response; the last frame stays on screen. */
        httpd_resp_send_chunk(req, "--" STREAM_BOUNDARY "--\r\n",
                              strlen("--" STREAM_BOUNDARY "--\r\n"));
        httpd_resp_send_chunk(req, NULL, 0);
    }
    return result;
}

/*
 * Start the dedicated live-stream server on STREAM_PORT.
 */
static void stream_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = STREAM_PORT;
    config.ctrl_port = STREAM_CTRL_PORT;
    config.stack_size = 6144;
    config.max_uri_handlers = 4;     /* /stream, /playback + spare */
    config.max_open_sockets = 2;     /* one viewer + one spare */
    config.lru_purge_enable = true;  /* a new viewer replaces a stale one */
    config.send_wait_timeout = STREAM_SEND_WAIT_TIMEOUT_S;

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    esp_err_t err = httpd_start(&stream_server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(STREAM_TAG, "Failed to start stream server: %s",
                 esp_err_to_name(err));
        stream_server = NULL;
        return;
    }

    httpd_uri_t playback_uri = {
        .uri = "/playback",
        .method = HTTP_GET,
        .handler = playback_handler,
        .user_ctx = NULL};

    err = httpd_register_uri_handler(stream_server, &stream_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(STREAM_TAG, "Failed to register /stream: %s",
                 esp_err_to_name(err));
        return;
    }

    err = httpd_register_uri_handler(stream_server, &playback_uri);
    if (err != ESP_OK)
    {
        ESP_LOGE(STREAM_TAG, "Failed to register /playback: %s",
                 esp_err_to_name(err));
    }

    ESP_LOGI(STREAM_TAG,
             "Stream server started on port %d (send_timeout=%ds, frame_delay=%dms)",
             STREAM_PORT, STREAM_SEND_WAIT_TIMEOUT_S, STREAM_FRAME_DELAY_MS);
}

/*
 * Start HTTP server
 */
void http_server_start(void)
{
    httpd_config_t config =
        HTTPD_DEFAULT_CONFIG();

    /*
     * Increase HTTPD task stack.
     */
    config.stack_size = 8192;

    /*
     * 9 handlers registered on this server; leave room for more so a new
     * endpoint doesn't fail silently.
     */
    config.max_uri_handlers = 16;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    httpd_uri_t photo_uri = {
        .uri = "/photo",
        .method = HTTP_GET,
        .handler = photo_handler,
        .user_ctx = NULL};

    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL};

    httpd_uri_t download_uri = {
        .uri = "/download",
        .method = HTTP_GET,
        .handler = download_handler,
        .user_ctx = NULL};

    httpd_uri_t capture_frames_uri = {
        .uri = "/capture_frames",
        .method = HTTP_GET,
        .handler = capture_frames_handler,
        .user_ctx = NULL};

    httpd_uri_t frame_uri = {
        .uri = "/frame",
        .method = HTTP_GET,
        .handler = frame_handler,
        .user_ctx = NULL};

    httpd_uri_t record_video_uri = {
        .uri = "/record_video",
        .method = HTTP_GET,
        .handler = record_video_handler,
        .user_ctx = NULL};

    httpd_uri_t recording_uri = {
        .uri = "/record.avi",
        .method = HTTP_GET,
        .handler = recording_download_handler,
        .user_ctx = NULL};

    httpd_uri_t recording_status_uri = {
        .uri = "/record_status",
        .method = HTTP_GET,
        .handler = recording_status_handler,
        .user_ctx = NULL};

    ESP_LOGI(
        TAG,
        "Starting HTTP server on port %d",
        config.server_port);

    ESP_LOGI(
        TAG,
        "HTTPD task stack size: %u bytes",
        (unsigned)config.stack_size);

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    const httpd_uri_t *uris[] = {
        &index_uri,
        &photo_uri,
        &capture_uri,
        &download_uri,
        &capture_frames_uri,
        &frame_uri,
        &record_video_uri,
        &recording_uri,
        &recording_status_uri,
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++)
    {
        esp_err_t err = httpd_register_uri_handler(server, uris[i]);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to register %s: %s",
                     uris[i]->uri, esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "HTTP server started");

    /*
     * Live stream runs on its own server so it can't block this one.
     */
    stream_server_start();
}