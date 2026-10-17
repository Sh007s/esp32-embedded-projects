/*
 * ESP-NOW link test - SENDER (self-contained, no extra components)
 *
 * - No Wi-Fi network: Wi-Fi is started only to use ESP-NOW, fixed channel.
 * - Auto-pairing: broadcasts HELLO to FF:FF:FF:FF:FF:FF every 500 ms until a
 *   receiver answers HELLO_ACK with its own MAC. That MAC is saved in NVS.
 * - Then every TEST_INTERVAL_S seconds sends TEST_PACKETS numbered packets
 *   (242-byte payload each), an END packet, and prints the receiver's report.
 * - After REPAIR_AFTER_FAILS unACKed sends in a row it pairs again.
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
#define TEST_PACKETS        1000
#define TEST_INTERVAL_S     10
#define HELLO_INTERVAL_MS   500
#define REPAIR_AFTER_FAILS  20

/* ------------------------- protocol (same in receiver) ------------------------- */
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

static const char *TAG = "sender";
static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct {
    uint8_t mac[6];
    int     rssi;
    int     len;
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
} rx_item_t;

static QueueHandle_t     s_rx_queue;
static SemaphoreHandle_t s_send_mutex, s_send_done, s_result_ready;
static volatile esp_now_send_status_t s_send_status;
static volatile bool     s_paired;
static uint8_t           s_peer[6];
static uint32_t          s_fail_streak;
static result_msg_t      s_result;

/* ---------------- NVS: remember the receiver's MAC ---------------- */
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

/* Send one packet and wait for the result (ACK for unicast). */
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
    h->role = ROLE_SENDER;
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

/* Send to the paired receiver; re-pair after too many failures. */
static esp_err_t send_to_peer(uint8_t type, const void *payload, size_t len)
{
    if (!s_paired) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = link_send(s_peer, type, payload, len, pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
        s_fail_streak = 0;
    } else if (++s_fail_streak >= REPAIR_AFTER_FAILS) {
        ESP_LOGW(TAG, "%d sends not ACKed in a row -> pairing again",
                 REPAIR_AFTER_FAILS);
        s_fail_streak = 0;
        s_paired = false;
    }
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
    xQueueSend(s_rx_queue, &it, 0);
}

/* ---------------- tasks ---------------- */
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
            h->role != ROLE_RECEIVER) {
            continue;
        }
        const uint8_t *payload = it.data + sizeof(*h);
        size_t plen = it.len - sizeof(*h);

        if (h->type == TYPE_HELLO_ACK) {
            if (!s_paired || memcmp(s_peer, it.mac, 6) != 0) {
                if (esp_now_is_peer_exist(s_peer) &&
                    memcmp(s_peer, it.mac, 6) != 0) {
                    esp_now_del_peer(s_peer);
                }
                memcpy(s_peer, it.mac, 6);
                if (add_peer(it.mac) == ESP_OK) {
                    nvs_save_peer(it.mac);
                    ESP_LOGI(TAG, "Paired with receiver " MACSTR " (rssi %d)",
                             MAC2STR(it.mac), it.rssi);
                }
            }
            s_fail_streak = 0;
            s_paired = true;
        } else if (h->type == TYPE_RESULT && s_paired &&
                   memcmp(s_peer, it.mac, 6) == 0 &&
                   plen >= sizeof(result_msg_t)) {
            memcpy(&s_result, payload, sizeof(s_result));
            xSemaphoreGive(s_result_ready);
        }
    }
}

/* Broadcast HELLO until a receiver answers. */
static void pair_task(void *arg)
{
    uint32_t n = 0;
    while (true) {
        if (!s_paired) {
            link_send(BROADCAST, TYPE_HELLO, NULL, 0, pdMS_TO_TICKS(100));
            if ((n++ % 10) == 0) {
                ESP_LOGI(TAG, "Looking for receiver (HELLO to FF:FF:FF:FF:FF:FF)...");
            }
        } else {
            n = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(HELLO_INTERVAL_MS));
    }
}

/* ---------------- test ---------------- */
static void run_test(uint32_t run_id)
{
    uint8_t buf[MAX_PAYLOAD];
    data_hdr_t *d = (data_hdr_t *)buf;
    uint32_t ok = 0, fail = 0;

    xSemaphoreTake(s_result_ready, 0);
    int64_t t0 = esp_timer_get_time();

    for (uint32_t seq = 0; seq < TEST_PACKETS; seq++) {
        d->run_id = run_id;
        d->seq = seq;
        d->total = TEST_PACKETS;
        for (uint32_t i = sizeof(*d); i < sizeof(buf); i++) {
            buf[i] = fill_byte(seq, i);
        }
        if (send_to_peer(TYPE_DATA, buf, sizeof(buf)) == ESP_OK) {
            ok++;
        } else {
            fail++;
        }
        if (!s_paired) {
            ESP_LOGW(TAG, "Link lost during run %lu at packet %lu",
                     (unsigned long)run_id, (unsigned long)seq);
            return;
        }
    }
    float secs = (esp_timer_get_time() - t0) / 1e6f;

    end_msg_t end = { run_id, TEST_PACKETS, ok };
    for (int i = 0; i < 3 && send_to_peer(TYPE_END, &end, sizeof(end)) != ESP_OK; i++) {
    }

    ESP_LOGI(TAG, "Run %lu sent %d x %u B in %.2f s: ACKed %lu, failed %lu, "
                  "%.0f pkt/s, %.1f KB/s",
             (unsigned long)run_id, TEST_PACKETS, (unsigned)sizeof(buf), secs,
             (unsigned long)ok, (unsigned long)fail,
             secs > 0 ? TEST_PACKETS / secs : 0,
             secs > 0 ? ok * sizeof(buf) / 1024.0f / secs : 0);

    /* Wait for THIS run's report. A late report from an earlier run (for
     * example after the receiver restarted mid-run) is skipped, not taken
     * as "no report". */
    bool got = false;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
    while (!got) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline ||
            xSemaphoreTake(s_result_ready, deadline - now) != pdTRUE) {
            break;
        }
        if (s_result.run_id == run_id) {
            got = true;
        } else {
            ESP_LOGI(TAG, "Ignoring late report for run %lu",
                     (unsigned long)s_result.run_id);
        }
    }
    if (got) {
        uint32_t lost = s_result.total - s_result.received;
        float pct = s_result.total ? 100.0f * lost / s_result.total : 0;
        ESP_LOGI(TAG, "Run %lu receiver report: got %lu/%lu, lost %lu (%.2f%%), "
                      "dup %lu, out-of-order %lu, corrupted %lu, rssi %d..%d -> %s",
                 (unsigned long)run_id,
                 (unsigned long)s_result.received, (unsigned long)s_result.total,
                 (unsigned long)lost, pct,
                 (unsigned long)s_result.duplicates,
                 (unsigned long)s_result.out_of_order,
                 (unsigned long)s_result.corrupted,
                 s_result.rssi_min, s_result.rssi_max,
                 (pct < 1.0f && s_result.corrupted == 0) ? "PASS" : "FAIL");
    } else {
        ESP_LOGW(TAG, "Run %lu: no report from receiver", (unsigned long)run_id);
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
    ESP_LOGI(TAG, "SENDER started, own MAC " MACSTR ", channel %d",
             MAC2STR(own), WIFI_CHANNEL);

    s_rx_queue = xQueueCreate(16, sizeof(rx_item_t));
    s_send_mutex = xSemaphoreCreateMutex();
    s_send_done = xSemaphoreCreateBinary();
    s_result_ready = xSemaphoreCreateBinary();

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_send));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    ESP_ERROR_CHECK(add_peer(BROADCAST));

    uint8_t saved[6];
    if (nvs_load_peer(saved)) {
        ESP_LOGI(TAG, "Last receiver (NVS): " MACSTR " - confirming with HELLO",
                 MAC2STR(saved));
    }

    xTaskCreate(rx_task, "rx", 4096, NULL, 6, NULL);
    xTaskCreate(pair_task, "pair", 3072, NULL, 4, NULL);

    uint32_t run_id = 0;
    while (true) {
        while (!s_paired) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        run_test(++run_id);
        vTaskDelay(pdMS_TO_TICKS(TEST_INTERVAL_S * 1000));
    }
}