#include "wifi_sta.h"

#include <string.h>

#include "freertos/event_groups.h"
#include "freertos/timers.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_sta";

#define QUICK_RETRIES      5
#define SLOW_RETRY_MS      5000
#define CONNECTED_BIT      BIT0

static EventGroupHandle_t s_events;
static TimerHandle_t s_retry_timer;
static int s_retry_count;

static void retry_timer_cb(TimerHandle_t t)
{
    ESP_LOGI(TAG, "Retrying Wi-Fi connection (slow retry every %d s)",
             SLOW_RETRY_MS / 1000);
    esp_wifi_connect();
}

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        if (s_retry_count < QUICK_RETRIES) {
            s_retry_count++;
            ESP_LOGI(TAG, "Retrying Wi-Fi connection (%d/%d)",
                     s_retry_count, QUICK_RETRIES);
            esp_wifi_connect();
        } else {
            if (s_retry_count == QUICK_RETRIES) {
                ESP_LOGW(TAG, "Wi-Fi still down after %d quick retries; "
                              "retrying every %d s",
                         QUICK_RETRIES, SLOW_RETRY_MS / 1000);
                s_retry_count++;
            }
            xTimerStart(s_retry_timer, 0);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        s_retry_count = 0;
        xTimerStop(s_retry_timer, 0);
        ESP_LOGI(TAG, "Wi-Fi connected, IP: " IPSTR ", channel %u",
                 IP2STR(&ev->ip_info.ip), wifi_sta_channel());
        xEventGroupSetBits(s_events, CONNECTED_BIT);
    }
}

esp_err_t wifi_sta_start(const char *ssid, const char *password)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_events = xEventGroupCreate();
    s_retry_timer = xTimerCreate("wifi_retry", pdMS_TO_TICKS(SLOW_RETRY_MS),
                                 pdFALSE, NULL, retry_timer_cb);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &event_handler, NULL));

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, password, sizeof(wc.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);
    return ESP_OK;
}

bool wifi_sta_wait_connected(TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(s_events, CONNECTED_BIT,
                                           pdFALSE, pdTRUE, timeout);
    return (bits & CONNECTED_BIT) != 0;
}

bool wifi_sta_is_connected(void)
{
    return (xEventGroupGetBits(s_events) & CONNECTED_BIT) != 0;
}

uint8_t wifi_sta_channel(void)
{
    uint8_t primary = 0;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&primary, &second) != ESP_OK) {
        return 0;
    }
    return primary;
}
