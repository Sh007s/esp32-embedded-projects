/*
 * ESP-NOW link test - RECEIVER (self-contained, no extra components)
 *
 * - No Wi-Fi network: Wi-Fi is started only to use ESP-NOW, fixed channel.
 * - Auto-pairing: when a sender's HELLO broadcast arrives, adds the sender as
 *   a peer, saves its MAC in NVS and replies HELLO_ACK (from its own MAC).
 * - Counts test packets per run (received, lost, duplicate, out-of-order,
 *   corrupted, RSSI), prints a summary and sends it back to the sender.
 *
 * Both boards must use the same WIFI_CHANNEL.
 * ESP-IDF v5.1.x, target esp32.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

/* ------------------------- settings ------------------------- */
#define WIFI_CHANNEL        1      /* same on both boards (1..13) */
#define MAX_TEST_PACKETS    2000

/* ------------------------- protocol (same in sender) ------------------------- */
#define LINK_MAGIC   0x4B4C4E45u   /* "ENLK" */
#define LINK_VERSION 1

enum { ROLE_SENDER = 1, ROLE_RECEIVER = 2 };
enum {
    TYPE_HELLO     = 0x01,
    TYPE_HELLO_ACK = 0x02,
    TYPE_DATA      = 0x10,
    TYPE_END       = 0x11,
    TYPE_RESULT    = 0x12,
};

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  role;
    uint8_t  reserved;
} link_hdr_t;

#define MAX_PAYLOAD (ESP_NOW_MAX_DATA_LEN - sizeof(link_hdr_t))   /* 242 */

typedef struct __attribute__((packed)) {
    uint32_t run_id, seq, total;
} data_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t run_id, total, sender_ok;
} end_msg_t;

typedef struct __attribute__((packed)) {
    uint32_t run_id, total, received, duplicates, out_of_order, corrupted;
    uint32_t duration_ms;
    int8_t   rssi_min, rssi_max;
} result_msg_t;

static inline uint8_t fill_byte(uint32_t seq, uint32_t i)
{
    return (uint8_t)(seq * 7u + i);
}
/* ------------------------------------------------------------------------------ */

static const char *TAG = "receiver";

typedef struct {
    uint8_t mac[6];
    int     rssi;
    int     len;
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
} rx_item_t;

static QueueHandle_t     s_rx_queue;
static SemaphoreHandle_t s_send_mutex, s_send_done;
static volatile esp_now_send_status_t s_send_status;
static bool              s_paired;
static uint8_t           s_peer[6];
static volatile uint32_t s_queue_drops;

/* Per-run statistics (only used from rx_task). */
static uint32_t s_run_id, s_total;
static uint8_t  s_seen[MAX_TEST_PACKETS / 8];
static uint32_t s_received, s_dup, s_ooo, s_corrupt, s_sender_ok;
static int64_t  s_first_us, s_last_us;
static int32_t  s_last_seq;
static int      s_rssi_min, s_rssi_max;
static bool     s_reported;

/* ---------------- NVS: remember the sender's MAC ---------------- */
static void nvs_save_peer(const uint8_t mac[6])
{
    nvs_handle_t h;
    if (nvs_open("espnow", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "peer", mac, 6);
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool nvs_load_peer(uint8_t mac[6])
{
    nvs_handle_t h;
    size_t len = 6;
    if (nvs_open("espnow", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_get_blob(h, "peer", mac, &len) == ESP_OK && len == 6;
    nvs_close(h);
    return ok;
}

/* ---------------- ESP-NOW helpers ---------------- */
static esp_err_t add_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }
    esp_now_peer_info_t p = {0};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;              /* current channel */
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    return esp_now_add_peer(&p);
}

static esp_err_t link_send(const uint8_t *mac, uint8_t type,
                           const void *payload, size_t len, TickType_t timeout)
{
    uint8_t buf[ESP_NOW_MAX_DATA_LEN];
    link_hdr_t *h = (link_hdr_t *)buf;

    if (len > MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }
    h->magic = LINK_MAGIC;
    h->version = LINK_VERSION;
    h->type = type;
    h->role = ROLE_RECEIVER;
    h->reserved = 0;
    if (len) {
        memcpy(buf + sizeof(*h), payload, len);
    }

    xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    xSemaphoreTake(s_send_done, 0);
    esp_err_t err = esp_now_send(mac, buf, sizeof(*h) + len);
    if (err == ESP_OK) {
        if (xSemaphoreTake(s_send_done, timeout) != pdTRUE) {
            err = ESP_ERR_TIMEOUT;
        } else if (s_send_status != ESP_NOW_SEND_SUCCESS) {
            err = ESP_FAIL;
        }
    }
    xSemaphoreGive(s_send_mutex);
    return err;
}

/* ---------------- callbacks (Wi-Fi task: keep short) ---------------- */
static void on_send(const uint8_t *mac, esp_now_send_status_t status)
{
    s_send_status = status;
    xSemaphoreGive(s_send_done);
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len <= 0 || len > ESP_NOW_MAX_DATA_LEN) {
        return;
    }
    rx_item_t it;
    memcpy(it.mac, info->src_addr, 6);
    it.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    it.len = len;
    memcpy(it.data, data, len);
    if (xQueueSend(s_rx_queue, &it, 0) != pdTRUE) {
        s_queue_drops++;
    }
}

/* ---------------- test statistics ---------------- */
static void reset_run(uint32_t run_id, uint32_t total)
{
    s_run_id = run_id;
    s_total = total;
    memset(s_seen, 0, sizeof(s_seen));
    s_received = s_dup = s_ooo = s_corrupt = s_sender_ok = 0;
    s_first_us = s_last_us = 0;
    s_last_seq = -1;
    s_rssi_min = 0;
    s_rssi_max = -127;
    s_reported = false;
}

static void report(const char *why)
{
    if (s_reported || s_run_id == 0) {
        return;
    }
    s_reported = true;

    uint32_t lost = s_total - s_received;
    float pct = s_total ? 100.0f * lost / s_total : 0;
    uint32_t ms = (uint32_t)((s_last_us - s_first_us) / 1000);

    ESP_LOGI(TAG, "Run %lu (%s): got %lu/%lu, lost %lu (%.2f%%), dup %lu, "
                  "out-of-order %lu, corrupted %lu, %lu ms, rssi %d..%d, "
                  "sender saw %lu ACKed, queue drops %lu -> %s",
             (unsigned long)s_run_id, why,
             (unsigned long)s_received, (unsigned long)s_total,
             (unsigned long)lost, pct,
             (unsigned long)s_dup, (unsigned long)s_ooo,
             (unsigned long)s_corrupt, (unsigned long)ms,
             s_rssi_min, s_rssi_max, (unsigned long)s_sender_ok,
             (unsigned long)s_queue_drops,
             (pct < 1.0f && s_corrupt == 0) ? "PASS" : "FAIL");
    s_queue_drops = 0;

    result_msg_t r = {
        .run_id = s_run_id, .total = s_total, .received = s_received,
        .duplicates = s_dup, .out_of_order = s_ooo, .corrupted = s_corrupt,
        .duration_ms = ms, .rssi_min = (int8_t)s_rssi_min,
        .rssi_max = (int8_t)s_rssi_max,
    };
    if (link_send(s_peer, TYPE_RESULT, &r, sizeof(r), pdMS_TO_TICKS(100)) != ESP_OK) {
        ESP_LOGW(TAG, "Report not delivered to sender");
    }
}

static void on_data(const uint8_t *p, size_t len, int rssi)
{
    if (len < sizeof(data_hdr_t)) {
        s_corrupt++;
        return;
    }
    const data_hdr_t *d = (const data_hdr_t *)p;

    if (d->run_id != s_run_id) {
        report("next run started before END");
        if (d->total == 0 || d->total > MAX_TEST_PACKETS) {
            return;
        }
        reset_run(d->run_id, d->total);
        ESP_LOGI(TAG, "Run %lu started (%lu packets)",
                 (unsigned long)d->run_id, (unsigned long)d->total);
    }
    if (d->seq >= s_total) {
        s_corrupt++;
        return;
    }

    int64_t now = esp_timer_get_time();
    if (s_first_us == 0) {
        s_first_us = now;
    }
    s_last_us = now;
    if (rssi < s_rssi_min) s_rssi_min = rssi;
    if (rssi > s_rssi_max) s_rssi_max = rssi;

    uint32_t byte = d->seq / 8, bit = 1u << (d->seq % 8);
    if (s_seen[byte] & bit) {
        s_dup++;
        return;
    }
    s_seen[byte] |= bit;

    if ((int32_t)d->seq < s_last_seq) {
        s_ooo++;
    }
    s_last_seq = (int32_t)d->seq;

    bool ok = (len == MAX_PAYLOAD);
    for (size_t i = sizeof(*d); ok && i < len; i++) {
        if (p[i] != fill_byte(d->seq, i)) {
            ok = false;
        }
    }
    if (!ok) {
        s_corrupt++;
    }
    s_received++;
}

/* ---------------- receive task ---------------- */
static void rx_task(void *arg)
{
    rx_item_t it;
    while (true) {
        if (xQueueReceive(s_rx_queue, &it, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (it.len < (int)sizeof(link_hdr_t)) {
            continue;
        }
        const link_hdr_t *h = (const link_hdr_t *)it.data;
        if (h->magic != LINK_MAGIC || h->version != LINK_VERSION ||
            h->role != ROLE_SENDER) {
            continue;
        }
        const uint8_t *payload = it.data + sizeof(*h);
        size_t plen = it.len - sizeof(*h);

        if (h->type == TYPE_HELLO) {
            /* Pair with whichever sender says HELLO (replaces an old one). */
            if (!s_paired || memcmp(s_peer, it.mac, 6) != 0) {
                if (s_paired && esp_now_is_peer_exist(s_peer)) {
                    esp_now_del_peer(s_peer);
                }
                memcpy(s_peer, it.mac, 6);
                if (add_peer(it.mac) != ESP_OK) {
                    ESP_LOGE(TAG, "esp_now_add_peer failed");
                    continue;
                }
                nvs_save_peer(it.mac);
                s_paired = true;
                ESP_LOGI(TAG, "Paired with sender " MACSTR " (rssi %d)",
                         MAC2STR(it.mac), it.rssi);
            }
            esp_err_t err = link_send(it.mac, TYPE_HELLO_ACK, NULL, 0,
                                      pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, "HELLO from " MACSTR " -> ACK %s",
                     MAC2STR(it.mac), err == ESP_OK ? "delivered" : "not delivered");
            continue;
        }

        /* Test packets only from the paired sender. */
        if (!s_paired || memcmp(s_peer, it.mac, 6) != 0) {
            continue;
        }
        if (h->type == TYPE_DATA) {
            on_data(payload, plen, it.rssi);
        } else if (h->type == TYPE_END && plen >= sizeof(end_msg_t)) {
            const end_msg_t *e = (const end_msg_t *)payload;
            if (e->run_id == s_run_id) {
                s_sender_ok = e->sender_ok;
                report("END received");
            }
        }
    }
}

/* ---------------- main ---------------- */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Wi-Fi only as the radio for ESP-NOW: no network, fixed channel. */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    uint8_t own[6];
    esp_wifi_get_mac(WIFI_IF_STA, own);
    ESP_LOGI(TAG, "RECEIVER started, own MAC " MACSTR ", channel %d",
             MAC2STR(own), WIFI_CHANNEL);

    s_rx_queue = xQueueCreate(32, sizeof(rx_item_t));
    s_send_mutex = xSemaphoreCreateMutex();
    s_send_done = xSemaphoreCreateBinary();

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_send));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    uint8_t saved[6];
    if (nvs_load_peer(saved) && add_peer(saved) == ESP_OK) {
        memcpy(s_peer, saved, 6);
        s_paired = true;
        ESP_LOGI(TAG, "Last sender (NVS): " MACSTR, MAC2STR(saved));
    }
    ESP_LOGI(TAG, "Waiting for sender HELLO...");

    xTaskCreate(rx_task, "rx", 4096, NULL, 6, NULL);
}