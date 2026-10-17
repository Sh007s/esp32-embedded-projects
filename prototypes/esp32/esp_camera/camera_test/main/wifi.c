#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define WIFI_SSID       CONFIG_ESP_WIFI_SSID
#define WIFI_PASSWORD   CONFIG_ESP_WIFI_PASSWORD
#define WIFI_MAX_RETRY  CONFIG_ESP_MAXIMUM_RETRY

static const char *TAG = "wifi";

static int retry_count = 0;

/*
 * After WIFI_MAX_RETRY quick retries, keep trying forever but slowly, so the
 * camera reconnects whenever the hotspot comes back (previously it gave up
 * permanently after about 10 seconds and needed a reboot).
 */
#define WIFI_SLOW_RETRY_MS 5000

static TimerHandle_t reconnect_timer = NULL;

static void reconnect_timer_cb(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "Retrying Wi-Fi connection (slow retry every %d s)",
             WIFI_SLOW_RETRY_MS / 1000);
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        ESP_LOGI(TAG, "Wi-Fi started");
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {

        if (retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            retry_count++;

            ESP_LOGI(TAG,
                     "Retrying Wi-Fi connection (%d/%d)",
                     retry_count,
                     WIFI_MAX_RETRY);
        } else {
            /* Don't give up: try again every few seconds until the AP returns.
             * A one-shot timer is used because the event handler must not block. */
            if (retry_count == WIFI_MAX_RETRY) {
                ESP_LOGW(TAG, "Wi-Fi still down after %d quick retries; "
                              "retrying every %d s",
                         WIFI_MAX_RETRY, WIFI_SLOW_RETRY_MS / 1000);
                retry_count++;
            }
            if (reconnect_timer != NULL) {
                xTimerStart(reconnect_timer, 0);
            }
        }

    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        retry_count = 0;
        if (reconnect_timer != NULL) {
            xTimerStop(reconnect_timer, 0);
        }

        ESP_LOGI(TAG,
                 "Wi-Fi connected, IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));
    }
}

void wifi_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    reconnect_timer = xTimerCreate("wifi_reconnect",
                                   pdMS_TO_TICKS(WIFI_SLOW_RETRY_MS),
                                   pdFALSE, /* one-shot */
                                   NULL,
                                   reconnect_timer_cb);

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg)
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        )
    );

    wifi_config_t wifi_config = {0};

    strncpy(
        (char *)wifi_config.sta.ssid,
        WIFI_SSID,
        sizeof(wifi_config.sta.ssid) - 1
    );

    strncpy(
        (char *)wifi_config.sta.password,
        WIFI_PASSWORD,
        sizeof(wifi_config.sta.password) - 1
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_start()
    );

    /*
     * Disable Wi-Fi power saving.
     * Useful for responsive image transfer.
     */
    ESP_ERROR_CHECK(
        esp_wifi_set_ps(WIFI_PS_NONE)
    );

    ESP_LOGI(TAG, "Wi-Fi initialization complete");
}