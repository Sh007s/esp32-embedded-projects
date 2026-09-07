#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_camera.h"

static const char *TAG = "http_server";

static httpd_handle_t server = NULL;

#define PHOTO_PATH "/sdcard/photo.jpg"


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
        "no-store"
    );

    return httpd_resp_send(
        req,
        html,
        HTTPD_RESP_USE_STRLEN
    );
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
        "image/jpeg"
    );

    /*
     * Prevent browser caching.
     */
    httpd_resp_set_hdr(
        req,
        "Cache-Control",
        "no-store, no-cache, must-revalidate"
    );

    httpd_resp_set_hdr(
        req,
        "Pragma",
        "no-cache"
    );

    httpd_resp_set_hdr(
        req,
        "Expires",
        "0"
    );

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
        0
    );

    ESP_LOGI(
        TAG,
        "Sent %s",
        PHOTO_PATH
    );

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
        "image/jpeg"
    );

    httpd_resp_set_hdr(
        req,
        "Content-Disposition",
        "attachment; filename=photo.jpg"
    );

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
        0
    );

    return ESP_OK;
}


/*
 * Capture a new JPEG and save it to SD card.
 */
static esp_err_t capture_handler(httpd_req_t *req)
{
    ESP_LOGI(
        TAG,
        "Capturing new photo..."
    );

    camera_fb_t *fb =
        esp_camera_fb_get();

    if (fb == NULL)
    {
        ESP_LOGE(
            TAG,
            "Camera capture failed"
        );

        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    if (fb->format != PIXFORMAT_JPEG)
    {
        ESP_LOGE(
            TAG,
            "Camera framebuffer is not JPEG"
        );

        esp_camera_fb_return(fb);

        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    FILE *file =
        fopen(
            PHOTO_PATH,
            "wb"
        );

    if (file == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to open %s",
            PHOTO_PATH
        );

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
            file
        );

    fclose(file);

    /*
     * Return camera frame buffer.
     */
    esp_camera_fb_return(fb);

    if (written != image_size)
    {
        ESP_LOGE(
            TAG,
            "Failed to save complete JPEG"
        );

        httpd_resp_send_500(req);

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "New photo saved: %u bytes",
        (unsigned)image_size
    );

    /*
     * No redirect.
     *
     * The browser JavaScript will request
     * /photo with a unique timestamp after
     * this request completes.
     */
    httpd_resp_set_type(
        req,
        "text/plain"
    );

    return httpd_resp_send(
        req,
        "OK",
        2
    );
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

    config.max_uri_handlers = 4;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL
    };

    httpd_uri_t photo_uri = {
        .uri = "/photo",
        .method = HTTP_GET,
        .handler = photo_handler,
        .user_ctx = NULL
    };

    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL
    };

    httpd_uri_t download_uri = {
        .uri = "/download",
        .method = HTTP_GET,
        .handler = download_handler,
        .user_ctx = NULL
    };

    ESP_LOGI(
        TAG,
        "Starting HTTP server on port %d",
        config.server_port
    );

    ESP_LOGI(
        TAG,
        "HTTPD task stack size: %u bytes",
        (unsigned)config.stack_size
    );

    if (httpd_start(
            &server,
            &config) == ESP_OK)
    {
        httpd_register_uri_handler(
            server,
            &index_uri
        );

        httpd_register_uri_handler(
            server,
            &photo_uri
        );

        httpd_register_uri_handler(
            server,
            &capture_uri
        );

        httpd_register_uri_handler(
            server,
            &download_uri
        );

        ESP_LOGI(
            TAG,
            "HTTP server started"
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Failed to start HTTP server"
        );
    }
}