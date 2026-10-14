
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_camera.h"
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>

static const char *TAG = "http_server";

static httpd_handle_t server = NULL;

#define PHOTO_PATH "/sdcard/photo.jpg"
#define FRAME_COUNT 30
#define FRAME_DELAY_MS 100

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

        "<p>"
        "<button onclick=\"captureFrames()\">"
        "Capture 30 Frames"
        "</button>"
        "</p>"
        "<h2>Captured Frames</h2>"
        "<div id=\"frames\" "
        "style=\"display:flex;flex-wrap:wrap;gap:8px;\"></div>"

        "<p>"
        "<a href=\"/download\">"
        "<button>Download Photo</button>"
        "</a>"
        "</p>"

        "<p id=\"status\"></p>"

        "<script>"

        "function refreshPhoto() {"
        "    document.getElementById('photo').src = "
        "        '/photo?t=' + new Date().getTime();"
        "}"

        "function captureFrames() {"
        "    const status = document.getElementById('status');"
        "    status.innerText = 'Capturing frames... please wait';"
        "    fetch('/capture_frames')"
        "        .then(function(response) {"
        "            return response.text().then(function(text) {"
        "                if (!response.ok) throw new Error(text);"
        "                return text;"
        "            });"
        "        })"
        "        .then(function(message) {"
        "            let result;"
        "            try { result = JSON.parse(message); }"
        "            catch (e) { throw new Error(message); }"
        "            status.innerText = 'Capture finished: ' + result.saved + "
        "                ' saved, ' + result.failed + ' failed.';"
        "            showFrames(result.frames || []);"
        "        })"
        "        .catch(function(error) {"
        "            status.innerText = 'Frame capture failed: ' + error.message;"
        "            console.error(error);"
        "        });"
        "}"
        "function showFrames(frameNumbers) {"
        "    const container = document.getElementById('frames');"
        "    container.innerHTML = '';"
        "    frameNumbers.forEach(function(number) {"
        "        const card = document.createElement('div');"
        "        const label = document.createElement('p');"
        "        const img = document.createElement('img');"
        "        label.innerText = 'Frame ' + number;"
        "        img.src = '/frame?num=' + number + '&t=' + Date.now();"
        "        img.alt = 'Frame ' + number;"
        "        img.style.width = '160px';"
        "        img.style.height = '120px';"
        "        img.style.objectFit = 'contain';"
        "        img.onerror = function() { card.remove(); };"
        "        card.appendChild(label);"
        "        card.appendChild(img);"
        "        container.appendChild(card);"
        "    });"
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

    config.max_uri_handlers = 6;

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

    ESP_LOGI(
        TAG,
        "Starting HTTP server on port %d",
        config.server_port);

    ESP_LOGI(
        TAG,
        "HTTPD task stack size: %u bytes",
        (unsigned)config.stack_size);

    if (httpd_start(
            &server,
            &config) == ESP_OK)
    {
        httpd_register_uri_handler(
            server,
            &index_uri);

        httpd_register_uri_handler(
            server,
            &photo_uri);

        httpd_register_uri_handler(
            server,
            &capture_uri);

        httpd_register_uri_handler(
            server,
            &download_uri);

        httpd_register_uri_handler(
            server,
            &capture_frames_uri);

        httpd_register_uri_handler(
            server,
            &frame_uri);

        ESP_LOGI(
            TAG,
            "HTTP server started");
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Failed to start HTTP server");
    }
}