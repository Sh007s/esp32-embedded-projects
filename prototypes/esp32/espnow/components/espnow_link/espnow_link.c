#include "espnow_link.h"

#include <string.h>

#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "nvs.h"

static const char *TAG = "espnow_link";

#define NVS_NAMESPACE       "espnow_link"
#define NVS_KEY_PEER        "peer_mac"
#define HELLO_INTERVAL_MS   500
#define RX_QUEUE_LEN        32
/* After this many unACKed sends in a row the initiator starts pairing again
 * (e.g. the responder board was replaced and has a new MAC). */
#define REPAIR_AFTER_FAILS  20

typedef struct {
    uint8_t mac[ESP_NOW_ETH_ALEN];
    int     rssi;
    int     len;
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
} rx_item_t;

static const uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] =
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static espnow_link_role_t s_role;
static espnow_link_rx_cb_t s_app_cb;

static QueueHandle_t s_rx_queue;
static SemaphoreHandle_t s_send_mutex;
static SemaphoreHandle_t s_send_done;
static volatile esp_now_send_status_t s_send_status;

static volatile bool s_paired;
static uint8_t s_peer[ESP_NOW_ETH_ALEN];
static uint32_t s_fail_streak;
static espnow_link_stats_t s_stats;

/* ------------------------------------------------------------------ */

static const char *mac_str(const uint8_t *mac, char buf[18])
{
    snprintf(buf, 18, MACSTR, MAC2STR(mac));
    return buf;
}

static bool nvs_load_peer(uint8_t mac[6])
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = ESP_NOW_ETH_ALEN;
    esp_err_t err = nvs_get_blob(h, NVS_KEY_PEER, mac, &len);
    nvs_close(h);
    return err == ESP_OK && len == ESP_NOW_ETH_ALEN;
}

static void nvs_save_peer(const uint8_t mac[6])
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed; peer not saved");
        return;
    }
    if (nvs_set_blob(h, NVS_KEY_PEER, mac, ESP_NOW_ETH_ALEN) == ESP_OK) {
        nvs_commit(h);
    }
    nvs_close(h);
}

static esp_err_t add_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }
    esp_now_peer_info_t p = {0};
    memcpy(p.peer_addr, mac, ESP_NOW_ETH_ALEN);
    p.channel = 0;            /* 0 = use the current Wi-Fi channel */
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    return esp_now_add_peer(&p);
}

/* Make mac the one unicast peer, replacing any previous one. */
static void set_peer(const uint8_t mac[6])
{
    char a[18], b[18];
    bool changed = memcmp(mac, s_peer, ESP_NOW_ETH_ALEN) != 0;

    if (changed && s_paired && esp_now_is_peer_exist(s_peer)) {
        ESP_LOGW(TAG, "Peer changed %s -> %s",
                 mac_str(s_peer, a), mac_str(mac, b));
        esp_now_del_peer(s_peer);
    }
    memcpy(s_peer, mac, ESP_NOW_ETH_ALEN);
    if (add_peer(mac) != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_add_peer failed");
        return;
    }
    if (changed) {
        nvs_save_peer(mac);
    }
    s_fail_streak = 0;
    if (!s_paired) {
        uint8_t ch = 0;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&ch, &second);
        ESP_LOGI(TAG, "Paired with %s (%s), channel %u",
                 mac_str(mac, a),
                 s_role == ESPNOW_LINK_ROLE_INITIATOR ? "responder" : "initiator",
                 ch);
    }
    s_paired = true;
}

/* Raw send of header + payload to one MAC, waiting for the send callback. */
static esp_err_t raw_send(const uint8_t *mac, uint8_t type,
                          const void *payload, size_t len, TickType_t timeout)
{
    uint8_t buf[ESP_NOW_MAX_DATA_LEN];
    espnow_link_hdr_t *h = (espnow_link_hdr_t *)buf;

    if (len > ESPNOW_LINK_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }
    h->magic = ESPNOW_LINK_MAGIC;
    h->version = ESPNOW_LINK_VERSION;
    h->type = type;
    h->role = (uint8_t)s_role;
    h->reserved = 0;
    if (len > 0) {
        memcpy(buf + sizeof(*h), payload, len);
    }

    xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    xSemaphoreTake(s_send_done, 0); /* clear any stale result */

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

/* ---------------- callbacks (run in the Wi-Fi task: keep short) ---------- */

static void on_send(const uint8_t *mac, esp_now_send_status_t status)
{
    s_send_status = status;
    xSemaphoreGive(s_send_done);
}

static void on_recv(const esp_now_recv_info_t *info,
                    const uint8_t *data, int len)
{
    if (info == NULL || data == NULL || len <= 0 ||
        len > ESP_NOW_MAX_DATA_LEN) {
        return;
    }
    rx_item_t item;
    memcpy(item.mac, info->src_addr, ESP_NOW_ETH_ALEN);
    item.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    item.len = len;
    memcpy(item.data, data, len);
    if (xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
        s_stats.rx_dropped++;
    }
}

/* ---------------- tasks ---------------- */

static void rx_task(void *arg)
{
    rx_item_t item;
    char a[18];

    while (true) {
        if (xQueueReceive(s_rx_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (item.len < (int)sizeof(espnow_link_hdr_t)) {
            s_stats.rx_invalid++;
            continue;
        }
        const espnow_link_hdr_t *h = (const espnow_link_hdr_t *)item.data;
        if (h->magic != ESPNOW_LINK_MAGIC || h->version != ESPNOW_LINK_VERSION) {
            s_stats.rx_invalid++;
            continue;
        }
        const uint8_t *payload = item.data + sizeof(*h);
        size_t plen = item.len - sizeof(*h);

        if (h->type == ESPNOW_LINK_TYPE_HELLO) {
            /* Only the responder answers, and only to an initiator. */
            if (s_role == ESPNOW_LINK_ROLE_RESPONDER &&
                h->role == ESPNOW_LINK_ROLE_INITIATOR) {
                set_peer(item.mac);
                esp_err_t err = raw_send(item.mac, ESPNOW_LINK_TYPE_HELLO_ACK,
                                         NULL, 0, pdMS_TO_TICKS(100));
                ESP_LOGI(TAG, "HELLO from %s (rssi %d) -> ACK %s",
                         mac_str(item.mac, a), item.rssi,
                         err == ESP_OK ? "delivered" : esp_err_to_name(err));
            }
            continue;
        }

        if (h->type == ESPNOW_LINK_TYPE_HELLO_ACK) {
            if (s_role == ESPNOW_LINK_ROLE_INITIATOR &&
                h->role == ESPNOW_LINK_ROLE_RESPONDER) {
                set_peer(item.mac);
            }
            continue;
        }

        /* Application packet: accept only from the paired peer. */
        if (!s_paired || memcmp(item.mac, s_peer, ESP_NOW_ETH_ALEN) != 0) {
            s_stats.rx_invalid++;
            continue;
        }
        s_stats.rx_packets++;
        if (s_app_cb) {
            s_app_cb(h->type, payload, plen, item.rssi);
        }
    }
}

/* Initiator only: broadcast HELLO until a responder answers. */
static void pair_task(void *arg)
{
    uint32_t n = 0;
    while (true) {
        if (!s_paired) {
            raw_send(BROADCAST_MAC, ESPNOW_LINK_TYPE_HELLO, NULL, 0,
                     pdMS_TO_TICKS(100));
            if ((n++ % 10) == 0) {
                ESP_LOGI(TAG, "Looking for a peer (HELLO broadcast)...");
            }
        } else {
            n = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(HELLO_INTERVAL_MS));
    }
}

/* ---------------- public API ---------------- */

esp_err_t espnow_link_start(espnow_link_role_t role, espnow_link_rx_cb_t rx_cb)
{
    char a[18];
    s_role = role;
    s_app_cb = rx_cb;

    s_rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(rx_item_t));
    s_send_mutex = xSemaphoreCreateMutex();
    s_send_done = xSemaphoreCreateBinary();
    if (!s_rx_queue || !s_send_mutex || !s_send_done) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_send));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    ESP_ERROR_CHECK(add_peer(BROADCAST_MAC));

    uint8_t own[6];
    esp_wifi_get_mac(WIFI_IF_STA, own);
    ESP_LOGI(TAG, "ESP-NOW started as %s, own MAC %s, max payload %u B",
             role == ESPNOW_LINK_ROLE_INITIATOR ? "INITIATOR" : "RESPONDER",
             mac_str(own, a), (unsigned)ESPNOW_LINK_MAX_PAYLOAD);

    uint8_t saved[6];
    if (nvs_load_peer(saved)) {
        ESP_LOGI(TAG, "Saved peer in NVS: %s", mac_str(saved, a));
        memcpy(s_peer, saved, sizeof(saved));
        /* The responder can talk to a known initiator straight away; the
         * initiator always confirms with HELLO so it knows the responder is
         * really there. */
        if (role == ESPNOW_LINK_ROLE_RESPONDER && add_peer(saved) == ESP_OK) {
            s_paired = true;
        }
    } else {
        ESP_LOGI(TAG, "No saved peer; waiting for pairing");
    }

    xTaskCreate(rx_task, "espnow_rx", 4096, NULL, 6, NULL);
    if (role == ESPNOW_LINK_ROLE_INITIATOR) {
        xTaskCreate(pair_task, "espnow_pair", 3072, NULL, 4, NULL);
    }
    return ESP_OK;
}

bool espnow_link_is_paired(void)
{
    return s_paired;
}

bool espnow_link_wait_paired(TickType_t timeout)
{
    TickType_t start = xTaskGetTickCount();
    while (!s_paired) {
        if (xTaskGetTickCount() - start >= timeout) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return true;
}

esp_err_t espnow_link_send(uint8_t type, const void *payload, size_t len,
                           TickType_t timeout)
{
    if (!s_paired) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = raw_send(s_peer, type, payload, len, timeout);
    if (err == ESP_OK) {
        s_stats.sent_ok++;
        s_fail_streak = 0;
    } else {
        s_stats.sent_fail++;
        if (++s_fail_streak >= REPAIR_AFTER_FAILS &&
            s_role == ESPNOW_LINK_ROLE_INITIATOR) {
            ESP_LOGW(TAG, "%u sends in a row not ACKed; pairing again",
                     (unsigned)s_fail_streak);
            s_paired = false; /* pair_task starts broadcasting HELLO again */
            s_fail_streak = 0;
        }
    }
    return err;
}

void espnow_link_get_peer(uint8_t mac[6])
{
    memcpy(mac, s_peer, ESP_NOW_ETH_ALEN);
}

void espnow_link_get_stats(espnow_link_stats_t *out)
{
    *out = s_stats;
}

void espnow_link_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}
