/*
 * ota_update.c - camera firmware update over Wi-Fi (H1.3).
 *
 * Update mode is entered only when the update button (IO13 to GND) is held
 * at power-on. Normal start-up is not changed when the button is not held.
 *
 * Update network: own access point (no router, no hotspot), WPA2 password
 * from ota_secret.h. Camera IP 192.168.4.1.
 *
 *   GET  /        -> short status text (version, running slot)
 *   POST /update  -> body = firmware .bin; written to the free OTA slot,
 *                    checked (esp_ota_end), set as boot slot, restart.
 *
 * After the restart the new firmware is "on trial" (rollback enabled);
 * main.c marks it valid once the camera and radio have started.
 */
#include "ota_update.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"

#if defined(__has_include)
#if __has_include("ota_secret.h")
#include "ota_secret.h"
#endif
#endif
#ifndef OTA_AP_SSID
#error "OTA: copy main/ota_secret.example.h to main/ota_secret.h and set OTA_AP_SSID / OTA_AP_PASS"
#endif

static const char *TAG = "OTA";

#define OTA_BUTTON_GPIO     GPIO_NUM_13   /* button to GND, internal pull-up */
#define OTA_LED_GPIO        GPIO_NUM_33   /* small red LED on the back, active low */
#define OTA_AP_CHANNEL      1
#define OTA_IDLE_TIMEOUT_US (10LL * 60 * 1000 * 1000)   /* 10 min without upload */
#define OTA_BUF_SIZE        4096

static uint8_t s_buf[OTA_BUF_SIZE];
static volatile bool s_upload_running;

/* ---------------------------------------------------------------- */

bool ota_update_button_held(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << OTA_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    vTaskDelay(pdMS_TO_TICKS(20));     /* let the pull-up settle */

    /* Held = low on 5 reads over ~50 ms (no false start from noise) */
    for (int i = 0; i < 5; i++) {
        if (gpio_get_level(OTA_BUTTON_GPIO) != 0) {
            gpio_reset_pin(OTA_BUTTON_GPIO);   /* leave IO13 as it was */
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

/* ---------------------------------------------------------------- */

static esp_err_t root_get(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    char txt[320];
    snprintf(txt, sizeof(txt),
             "BikeVision camera - update mode\n"
             "Firmware: %s (built %s %s)\n"
             "Running: %s | next upload goes to: %s (%lu KB max)\n"
             "Upload:  curl --data-binary @build/camera_test.bin http://192.168.4.1/update\n",
             app->version, app->date, app->time,
             run ? run->label : "?", next ? next->label : "?",
             next ? (unsigned long)(next->size / 1024) : 0UL);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, txt);
}

static esp_err_t fail(httpd_req_t *req, const char *status, const char *msg)
{
    ESP_LOGE(TAG, "%s", msg);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, msg);
    return ESP_FAIL;
}

static esp_err_t update_post(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        return fail(req, "500 Internal Server Error", "ERROR: no free OTA slot\n");
    }
    if (req->content_len == 0) {
        return fail(req, "400 Bad Request", "ERROR: empty upload\n");
    }
    if (req->content_len > part->size) {
        return fail(req, "400 Bad Request", "ERROR: firmware is bigger than the OTA slot\n");
    }

    s_upload_running = true;
    ESP_LOGW(TAG, "Upload started: %u bytes -> %s at 0x%lx",
             (unsigned)req->content_len, part->label, (unsigned long)part->address);

    esp_ota_handle_t h = 0;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h);
    if (err != ESP_OK) {
        s_upload_running = false;
        return fail(req, "500 Internal Server Error", "ERROR: esp_ota_begin failed\n");
    }

    int64_t t0 = esp_timer_get_time();
    size_t done = 0, next_log = 100 * 1024;
    int timeouts = 0;

    while (done < req->content_len) {
        size_t want = req->content_len - done;
        if (want > sizeof(s_buf)) {
            want = sizeof(s_buf);
        }
        int n = httpd_req_recv(req, (char *)s_buf, want);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) {
            continue;                    /* slow Wi-Fi moment: try again */
        }
        if (n <= 0) {
            esp_ota_abort(h);
            s_upload_running = false;
            return fail(req, "500 Internal Server Error", "ERROR: upload interrupted\n");
        }
        timeouts = 0;

        err = esp_ota_write(h, s_buf, n);
        if (err != ESP_OK) {
            esp_ota_abort(h);
            s_upload_running = false;
            return fail(req, "500 Internal Server Error", "ERROR: flash write failed\n");
        }
        done += n;
        if (done >= next_log) {
            ESP_LOGI(TAG, "  %u / %u KB", (unsigned)(done / 1024),
                     (unsigned)(req->content_len / 1024));
            next_log += 100 * 1024;
        }
    }

    /* esp_ota_end checks the image (header, SHA-256) before it is used */
    err = esp_ota_end(h);
    if (err != ESP_OK) {
        s_upload_running = false;
        return fail(req, "400 Bad Request",
                    err == ESP_ERR_OTA_VALIDATE_FAILED
                        ? "ERROR: not a valid firmware image (nothing changed)\n"
                        : "ERROR: esp_ota_end failed (nothing changed)\n");
    }
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        s_upload_running = false;
        return fail(req, "500 Internal Server Error", "ERROR: could not set the boot slot\n");
    }

    uint32_t ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    char txt[160];
    snprintf(txt, sizeof(txt),
             "OK: %u bytes written to %s in %lu ms, restarting into the new firmware\n",
             (unsigned)done, part->label, (unsigned long)ms);
    ESP_LOGW(TAG, "%s", txt);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, txt);

    vTaskDelay(pdMS_TO_TICKS(800));      /* let the reply reach the laptop */
    esp_restart();
    return ESP_OK;
}

/* ---------------------------------------------------------------- */

static void start_update_network(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, OTA_AP_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(OTA_AP_SSID);
    strncpy((char *)ap.ap.password, OTA_AP_PASS, sizeof(ap.ap.password));
    ap.ap.channel = OTA_AP_CHANNEL;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 2;
    ap.ap.beacon_interval = 100;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    ESP_LOGW(TAG, "Update network \"%s\" started, AP MAC " MACSTR ", IP 192.168.4.1",
             OTA_AP_SSID, MAC2STR(mac));
}

static void start_http(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;
    cfg.stack_size = 6144;

    httpd_handle_t srv = NULL;
    ESP_ERROR_CHECK(httpd_start(&srv, &cfg));

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t upd = { .uri = "/update", .method = HTTP_POST, .handler = update_post };
    httpd_register_uri_handler(srv, &root);
    httpd_register_uri_handler(srv, &upd);
}

void ota_update_mode_run(void)
{
    ESP_LOGW(TAG, "=== UPDATE MODE (button on IO13 held at power-on) ===");
    ESP_LOGW(TAG, "Camera, SD card and display link are NOT started");

    gpio_reset_pin(OTA_LED_GPIO);
    gpio_set_direction(OTA_LED_GPIO, GPIO_MODE_OUTPUT);

    start_update_network();
    start_http();

    ESP_LOGW(TAG, "Laptop: join \"%s\", then run in the camera_node folder:", OTA_AP_SSID);
    ESP_LOGW(TAG, "  curl --data-binary @build/camera_test.bin http://192.168.4.1/update");
    ESP_LOGW(TAG, "No upload within 10 minutes -> normal restart");

    int64_t t0 = esp_timer_get_time();
    bool led = false;
    while (true) {
        led = !led;
        gpio_set_level(OTA_LED_GPIO, led ? 0 : 1);   /* active low */
        vTaskDelay(pdMS_TO_TICKS(s_upload_running ? 100 : 500));

        if (!s_upload_running && esp_timer_get_time() - t0 > OTA_IDLE_TIMEOUT_US) {
            ESP_LOGW(TAG, "No upload for 10 minutes - restarting normally");
            esp_restart();
        }
    }
}
