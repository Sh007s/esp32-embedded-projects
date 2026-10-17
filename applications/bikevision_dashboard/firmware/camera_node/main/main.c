#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "wifi.h"
#include "http_server.h"
#include "camera.h"
#include "esp_camera.h"
#include "sdcard.h"
#include "espnow_cam.h"
#include <errno.h>
#include <string.h>

#define LED_GPIO GPIO_NUM_1 // for build_led pinout is 4
#define FRAME_COUNT 30
#define FRAME_DELAY_MS 100

#define PHOTO_PATH "/sdcard/photo.jpg"
#define FRAME_PATH_FORMAT "/sdcard/frame%03d.jpg"

static const char *TAG = "MAIN";

/*
 * Save a JPEG frame to the first available numbered filename.
 *
 * Existing frame files are not deliberately overwritten.
 * This is a check-then-create approach, not an atomic exclusive
 * create; use a single writer to this SD card.
 */
static esp_err_t save_frame_without_overwrite(
    const camera_fb_t *fb,
    int starting_number,
    int *saved_number)
{
    if (fb == NULL || fb->buf == NULL || fb->len == 0)
    {
        ESP_LOGE(TAG, "Invalid frame buffer");
        return ESP_ERR_INVALID_ARG;
    }

    char filename[64];
    int frame_number = starting_number;

    while (frame_number < 1000000)
    {
        snprintf(filename, sizeof(filename),
                 FRAME_PATH_FORMAT, frame_number);

        /*
         * If opening for reading succeeds, the filename exists.
         * Skip it and try the next number.
         */
        FILE *existing = fopen(filename, "rb");

        if (existing != NULL)
        {
            fclose(existing);
            frame_number++;
            continue;
        }

        /*
         * The file was not found (or could not be opened for reading).
         * Try creating it and report the underlying error if this fails.
         */
        errno = 0;
        FILE *file = fopen(filename, "w");

        if (file == NULL)
        {
            int open_errno = errno;
            ESP_LOGE(TAG,
                     "Cannot create %s: errno=%d (%s)",
                     filename,
                     open_errno,
                     strerror(open_errno));
            return ESP_FAIL;
        }

        size_t written = fwrite(fb->buf, 1, fb->len, file);
        int write_errno = errno;
        int close_result = fclose(file);

        if (written != fb->len || close_result != 0)
        {
            ESP_LOGE(TAG,
                     "Frame write failed: %s, wrote %u/%u bytes, "
                     "close=%d, errno=%d (%s)",
                     filename,
                     (unsigned int)written,
                     (unsigned int)fb->len,
                     close_result,
                     write_errno,
                     strerror(write_errno));
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Saved %s (%u bytes)",
                 filename, (unsigned int)written);

        if (saved_number != NULL)
        {
            *saved_number = frame_number;
        }

        return ESP_OK;
    }

    ESP_LOGE(TAG, "Could not find an available frame filename");
    return ESP_FAIL;
}

/*
 * Save the initial photo only if photo.jpg does not already exist.
 */
static void capture_initial_photo(void)
{
    ESP_LOGI(TAG, "Capturing initial image...");

    camera_fb_t *fb = esp_camera_fb_get();

    if (fb == NULL)
    {
        ESP_LOGE(TAG, "Failed to capture initial image");
        return;
    }

    if (fb->format != PIXFORMAT_JPEG)
    {
        ESP_LOGE(TAG, "Initial frame is not JPEG");
        esp_camera_fb_return(fb);
        return;
    }

    ESP_LOGI(TAG, "JPEG Header: %02X %02X", fb->buf[0], fb->buf[1]);
    ESP_LOGI(TAG, "Image captured: %dx%d, %u bytes",
             fb->width, fb->height, (unsigned int)fb->len);

    FILE *existing = fopen(PHOTO_PATH, "rb");

    if (existing != NULL)
    {
        fclose(existing);
        ESP_LOGW(TAG, "%s already exists; keeping existing file",
                 PHOTO_PATH);
        esp_camera_fb_return(fb);
        return;
    }

    errno = 0;
    FILE *file = fopen(PHOTO_PATH, "wb");

    if (file == NULL)
    {
        int open_errno = errno;
        ESP_LOGE(TAG, "Cannot create %s: errno=%d (%s)",
                 PHOTO_PATH, open_errno, strerror(open_errno));
        esp_camera_fb_return(fb);
        return;
    }

    size_t written = fwrite(fb->buf, 1, fb->len, file);
    int write_errno = errno;
    int close_result = fclose(file);

    if (written == fb->len && close_result == 0)
    {
        ESP_LOGI(TAG, "Photo saved: %s (%u bytes)",
                 PHOTO_PATH, (unsigned int)written);
    }
    else
    {
        ESP_LOGE(TAG,
                 "Photo write failed: wrote %u/%u bytes, "
                 "close=%d, errno=%d (%s)",
                 (unsigned int)written,
                 (unsigned int)fb->len,
                 close_result,
                 write_errno,
                 strerror(write_errno));
    }

    esp_camera_fb_return(fb);
}

/*
 * Capture and save a batch of JPEG frames.
 * This function is called after Wi-Fi and HTTP startup so the
 * website is available even if frame capture encounters errors.
 */
static void capture_multiple_frames(void)
{
    ESP_LOGI(TAG, "Starting multiple-frame capture...");
    ESP_LOGI(TAG, "Requested frames: %d", FRAME_COUNT);
    ESP_LOGI(TAG, "Requested delay: %d ms", FRAME_DELAY_MS);

    int saved_count = 0;
    int next_frame_number = 0;

    for (int i = 0; i < FRAME_COUNT; i++)
    {
        camera_fb_t *fb = esp_camera_fb_get();

        if (fb == NULL)
        {
            ESP_LOGE(TAG, "Frame %d capture failed", i);
            vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
            continue;
        }

        if (fb->format != PIXFORMAT_JPEG)
        {
            ESP_LOGE(TAG, "Frame %d is not JPEG", i);
            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
            continue;
        }

        ESP_LOGI(TAG, "Captured batch frame %d: %dx%d, %u bytes",
                 i,
                 fb->width,
                 fb->height,
                 (unsigned int)fb->len);

        int saved_number = -1;
        esp_err_t save_result =
            save_frame_without_overwrite(fb,
                                         next_frame_number,
                                         &saved_number);

        if (save_result == ESP_OK)
        {
            saved_count++;
            next_frame_number = saved_number + 1;
        }
        else
        {
            /*
             * Do not repeatedly retry every frame if the SD card
             * cannot create files; one failure is enough to stop
             * this batch and leave the website running.
             */
            esp_camera_fb_return(fb);
            ESP_LOGE(TAG,
                     "Stopping frame batch after file-save failure");
            break;
        }

        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(FRAME_DELAY_MS));
    }

    ESP_LOGI(TAG, "Multiple-frame capture finished: saved %d of %d",
             saved_count, FRAME_COUNT);
}

static void test_sd_file_creation(void)
{
    const char *text_path = "/sdcard/test.txt";
const char *frame_path = "/sdcard/frame000.jpg";
    FILE *file = NULL;
    errno = 0;

    /*
     * Test 1: Create and write a simple text file.
     * Preserve it if it already exists.
     */
    file = fopen(text_path, "rb");

    if (file != NULL)
    {
        fclose(file);
        ESP_LOGW("SD_TEST",
                 "%s already exists; preserving it",
                 text_path);
    }
    else
    {
        int read_errno = errno;

        if (read_errno != ENOENT)
        {
            ESP_LOGE("SD_TEST",
                     "Cannot check %s: errno=%d (%s)",
                     text_path,
                     read_errno,
                     strerror(read_errno));
        }
        else
        {
            errno = 0;
            file = fopen(text_path, "wb");

            if (file == NULL)
            {
                int err = errno;
                ESP_LOGE("SD_TEST",
                         "Cannot create %s: errno=%d (%s)",
                         text_path,
                         err,
                         strerror(err));
            }
            else
            {
                const char *message = "SD write test\n";
                size_t expected = strlen(message);
                size_t written =
                    fwrite(message, 1, expected, file);

                int write_errno = errno;
                int close_result = fclose(file);
                file = NULL;

                if (written == expected && close_result == 0)
                {
                    ESP_LOGI("SD_TEST",
                             "Text test passed: %u bytes written",
                             (unsigned int)written);
                }
                else
                {
                    ESP_LOGE("SD_TEST",
                             "Text write failed: wrote %u/%u, "
                             "close=%d, errno=%d (%s)",
                             (unsigned int)written,
                             (unsigned int)expected,
                             close_result,
                             write_errno,
                             strerror(write_errno));
                }
            }
        }
    }

    /*
     * Test 2: Check whether the numbered JPEG path can be opened.
     * Never truncate or overwrite an existing frame.
     */
    errno = 0;
    file = fopen(frame_path, "rb");

    if (file != NULL)
    {
        fclose(file);
        ESP_LOGW("SD_TEST",
                 "%s already exists; preserving it",
                 frame_path);
        return;
    }

    int read_errno = errno;

    if (read_errno != ENOENT)
    {
        ESP_LOGE("SD_TEST",
                 "Cannot check %s: errno=%d (%s)",
                 frame_path,
                 read_errno,
                 strerror(read_errno));
        return;
    }

    /*
     * Use exclusive creation ("wx") so an existing file cannot
     * be truncated if it appears between the check and creation.
     */
    errno = 0;
    file = fopen(frame_path, "wx");

    if (file == NULL)
    {
        int err = errno;
        ESP_LOGE("SD_TEST",
                 "Cannot create %s: errno=%d (%s)",
                 frame_path,
                 err,
                 strerror(err));
        return;
    }

    ESP_LOGI("SD_TEST",
             "Frame filename opened successfully: %s",
             frame_path);

    if (fclose(file) != 0)
    {
        int err = errno;
        ESP_LOGE("SD_TEST",
                 "Close failed for %s: errno=%d (%s)",
                 frame_path,
                 err,
                 strerror(err));
        return;
    }

    ESP_LOGI("SD_TEST",
             "Frame filename test passed");
}

void app_main(void)
{
    /*
     * Configure GPIO4 only if you need to control the flash LED.
     * On many ESP32-CAM boards GPIO4 is the flash LED.
     */
    // gpio_reset_pin(LED_GPIO);
    // gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    // gpio_set_level(LED_GPIO, 0);

    printf("\n");
    printf("=================================\n");
    printf("      ESP32-CAM Camera Test\n");
    printf("=================================\n");

    /*
     * 1. Initialize camera.
     */
    ESP_LOGI(TAG, "Initializing camera...");
    esp_err_t ret = camera_init();

    vTaskDelay(pdMS_TO_TICKS(500));

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera initialization failed: 0x%x", ret);

        while (1)
        {
            // LED GPIO is not configured above; keep this diagnostic
            // blink disabled unless you explicitly enable GPIO setup.
            ESP_LOGE(TAG, "Camera initialization failed; restarting not attempted");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    ESP_LOGI(TAG, "Camera initialized successfully.");

    /*
     * 2. Initialize SD card.
     */
    ESP_LOGI(TAG, "Initializing SD card...");

    ret = sdcard_init();

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD card initialization failed: 0x%x", ret);
        return;
    }
test_sd_file_creation();
    /*
     * 3. Warm up the camera.
     */
    ESP_LOGI(TAG, "Warming up camera...");

    for (int i = 0; i < 5; i++)
    {
        camera_fb_t *tmp = esp_camera_fb_get();

        if (tmp != NULL)
        {
            esp_camera_fb_return(tmp);
        }
        else
        {
            ESP_LOGW(TAG, "Warm-up capture %d failed", i);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Camera warm-up complete.");

    /*
     * 4. Save the initial single photo, preserving photo.jpg
     *    if it already exists.
     */
    capture_initial_photo();

    /*
     * 5. Start Wi-Fi and HTTP before batch capture.
     *    This keeps the website available while frames are saved.
     */
    ESP_LOGI(TAG, "Starting Wi-Fi...");
    wifi_init();

    ESP_LOGI(TAG, "Starting HTTP server...");
    http_server_start();

    /*
     * 5b. Send camera frames to the display board over ESP-NOW.
     *     Uses the hotspot's Wi-Fi channel; the display finds it by scanning.
     */
    ESP_LOGI(TAG, "Starting ESP-NOW link to display...");
    espnow_cam_start();

    /*
     * 6. Video recording is started on demand through /record_video.
     * Do not capture a batch of standalone JPEG frames at boot.
     */

    /*
     * 7. Keep app_main alive.
     */
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}