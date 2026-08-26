#include "tft.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_err.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "TFT_IMAGE_TEST";

// ========================================================
// 2. INITIALIZE TFT
// ========================================================

TFT tft(0);
// ============================================================
// SPIFFS INITIALIZATION
// ============================================================

static bool initSPIFFS()
{
    ESP_LOGI(TAG, "Mounting SPIFFS...");

    esp_vfs_spiffs_conf_t conf = {};

    conf.base_path = "/spiffs";
    conf.partition_label = "storage";
    conf.max_files = 10;
    conf.format_if_mount_failed = false;

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return false;
    }

    size_t total = 0;
    size_t used = 0;

    ret = esp_spiffs_info("storage", &total, &used);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS information: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "SPIFFS Total: %u bytes", (unsigned)total);

    ESP_LOGI(TAG, "SPIFFS Used : %u bytes", (unsigned)used);

    return true;
}

// ============================================================
// CHECK FILE
// ============================================================

static bool checkFile(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
    {
        ESP_LOGE(TAG, "File NOT found: %s", path);
        return false;
    }

    ESP_LOGI(TAG, "File found: %s", path);
    ESP_LOGI(TAG, "File size: %ld bytes", (long)st.st_size);
    return true;
}

// ============================================================
// DISPLAY ONE BMP + MEASURE TIME
// ============================================================

static bool displayBMP(TFT &tft, const char *path)
{
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "BMP: %s", path);
    ESP_LOGI(TAG, "----------------------------------------");

    if (!checkFile(path))
    {
        return false;
    }

    // Clear screen
      tft.fillScreen(TFT_BLACK);

    // Small delay so display clear is finished
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Starting BMP display...");

    int64_t start = esp_timer_get_time();

    bool result = tft.drawBmpFile(path, 0, 0);

    int64_t end = esp_timer_get_time();

    int64_t elapsed = end - start;

    if (result)
    {
        ESP_LOGI(TAG, "BMP displayed successfully");
        ESP_LOGI(TAG, "BMP DISPLAY TIME = %lld us", elapsed);
        ESP_LOGI(TAG, "BMP DISPLAY TIME = %.3f ms", elapsed / 1000.0);
        ESP_LOGI(TAG, "BMP DISPLAY TIME = %.3f sec", elapsed / 1000000.0);
    }
    else
    {
        ESP_LOGE(TAG, "BMP display FAILED");
    }

    return result;
}

// ========================================================
// DISPLAY ONE JPEG + MEASURE TIME
// ========================================================

static bool displayJPEG(TFT &tft, const char *path)
{
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "JPEG: %s", path);
    ESP_LOGI(TAG, "----------------------------------------");

    if (!checkFile(path))
    {
        return false;
    }

    ESP_LOGI(TAG, "Starting JPEG display...");

    int64_t start = esp_timer_get_time();

    // ====================================================
    // EXISTING JPEG FUNCTION
    // ====================================================

    bool result = tft.drawJpgFile(path, 0, 0);

    int64_t end = esp_timer_get_time();

    int64_t elapsed = end - start;

    if (result)
    {
        ESP_LOGI(TAG, "JPEG displayed successfully");
        ESP_LOGI(TAG, "JPEG DISPLAY TIME = %lld us", elapsed);
        ESP_LOGI(TAG, "JPEG DISPLAY TIME = %.3f ms", elapsed / 1000.0);
        ESP_LOGI(TAG, "JPEG DISPLAY TIME = %.3f sec", elapsed / 1000000.0);
    }
    else
    {
        ESP_LOGE(TAG, "JPEG display FAILED");
    }

    return result;
}

// ========================================================
// JPEG TEST
// ========================================================

static void Test_JPEG_File()
{
    // ====================================================
    // CHANGE THESE TO YOUR ACTUAL JPEG FILES
    // ====================================================

    const char *jpegFiles[] =
        {
            "/spiffs/Delaware 320x240.jpg",
            "/spiffs/Wall_e.jpg",
            "/spiffs/Tiger_320x240.jpg",
            "/spiffs/Wallpaper 320x240.jpg"};

    const int jpegCount =
        sizeof(jpegFiles) / sizeof(jpegFiles[0]);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "       JPEG IMAGE TEST");
    ESP_LOGI(TAG, "========================================");

    for (int i = 0; i < jpegCount; i++)
    {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "JPEG TEST %d / %d", i + 1, jpegCount);
        ESP_LOGI(TAG, "========================================");

        displayJPEG(tft, jpegFiles[i]);

        // Keep JPEG on screen for 3 seconds.
        // No black screen between images.
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "       ALL JPEG TESTS COMPLETE");
    ESP_LOGI(TAG, "========================================");
}

// ========================================================
// 3. BMP TESTS
// ========================================================

void Test_BMP_File()
{
    const char *bmpFiles[] =
        {
            "/spiffs/tiger.bmp",
            "/spiffs/flower.bmp",
            "/spiffs/Wall_e.bmp",
            "/spiffs/RedRose.bmp",
            "/spiffs/tree.bmp"};

    const int bmpCount = sizeof(bmpFiles) / sizeof(bmpFiles[0]);

    // ========================================================
    // DISPLAY EACH BMP
    // ========================================================

    for (int i = 0; i < bmpCount; i++)
    {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "BMP TEST %d / %d", i + 1, bmpCount);
        ESP_LOGI(TAG, "========================================");

        displayBMP(tft, bmpFiles[i]);

        // Keep image on screen for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // ========================================================
    // COMPLETE
    // ========================================================

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "       ALL BMP TESTS COMPLETE");
    ESP_LOGI(TAG, "========================================");
}

// ============================================================
// DISPLAY ONE GIF + MEASURE TIME
// ============================================================

static bool displayGIF(TFT &tft, const char *path)
{
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "GIF: %s", path);
    ESP_LOGI(TAG, "----------------------------------------");

    if (!checkFile(path))
    {
        return false;
    }

    // Clear TFT
    tft.fillScreen(TFT_BLACK);

    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Starting GIF display...");

    int64_t start = esp_timer_get_time();

    bool result = tft.drawGifFile(path, 0, 0, 1);

    int64_t end = esp_timer_get_time();

    int64_t elapsed = end - start;

    if (result)
    {
        ESP_LOGI(TAG, "GIF displayed successfully");
        ESP_LOGI(TAG, "GIF DISPLAY TIME = %lld us", elapsed);
        ESP_LOGI(TAG, "GIF DISPLAY TIME = %.3f ms", elapsed / 1000.0);
        ESP_LOGI(TAG, "GIF DISPLAY TIME = %.3f sec", elapsed / 1000000.0);
    }
    else
    {
        ESP_LOGE(TAG, "GIF display FAILED");
    }

    return result;
}

// ============================================================
// GIF TEST
// ============================================================

void Test_GIF_File()
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "       GIF IMAGE TEST");
    ESP_LOGI(TAG, "========================================");

    // --------------------------------------------------------
    // Put your GIF file names here
    // --------------------------------------------------------

    const char *gifFiles[] =
        {
            "/spiffs/birds.gif",
            "/spiffs/clock.gif",
            "/spiffs/Radar.gif",
            "/spiffs/tap.gif"};

    const int gifCount = sizeof(gifFiles) / sizeof(gifFiles[0]);

    // --------------------------------------------------------
    // Test GIF files
    // --------------------------------------------------------

    for (int i = 0; i < gifCount; i++)
    {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "GIF TEST %d / %d", i + 1, gifCount);
        ESP_LOGI(TAG, "========================================");
        displayGIF(tft, gifFiles[i]);

        // Keep result on screen
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "       ALL GIF TESTS COMPLETE");
    ESP_LOGI(TAG, "========================================");
}

// ============================================================
// MAIN
// ============================================================

extern "C" void app_main()
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "       TFT SPIFFS BMP TEST");
    ESP_LOGI(TAG, "       ESP-IDF");
    ESP_LOGI(TAG, "========================================");

    // ========================================================
    // 1. INITIALIZE SPIFFS
    // ========================================================

    if (!initSPIFFS())
    {
        ESP_LOGE(TAG, "SPIFFS initialization failed!");

        while (true)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    tft.begin();

    // Keep 30 MHz for this test
    tft.setFrequency(50000000);

    // Landscape
    tft.setRotation(3);

    ESP_LOGI(TAG, "TFT Width  : %d", tft.width());

    ESP_LOGI(TAG, "TFT Height : %d", tft.height());

    // ====================================================
    // 4. BMP TEST
    // ====================================================

    Test_BMP_File();

    // ====================================================
    // 5. JPEG TEST
    // ====================================================

    //  Test_JPEG_File();

    // ========================================================
    // GIF TEST
    // ========================================================

    // Test_GIF_File();

    // ====================================================
    // KEEP APPLICATION RUNNING
    // ====================================================

    while (true)
    {
        // Test_GIF_File();
        Test_BMP_File();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}