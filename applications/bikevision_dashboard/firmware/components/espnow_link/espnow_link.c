#include "espnow_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "espnow_link";

/* ---------------- tuning ---------------- */
#define HELLO_INTERVAL_MS   200
#define PING_INTERVAL_MS    1000
#define LINK_TIMEOUT_MS     3000   /* nothing ACKed / heard -> link down */
#define SCAN_DWELL_MS       300    /* receiver: time per channel while scanning */
#define CHUNK_RETRIES       3
#define SEND_TIMEOUT_MS     100    /* wait for one packet's MAC-layer result */

/* BikeVision 12fps plan, test B7: ESP-NOW radio speed. The default is 1 Mbps
 * (slowest, longest range). Camera and display are 1-2 m apart, so a faster
 * rate is fine. Set in espnow_link_radio_start(), so it applies to whichever
 * board is flashed with this file.
 * Other choices: WIFI_PHY_RATE_11M_L, WIFI_PHY_RATE_12M, WIFI_PHY_RATE_54M. */
#ifndef ESPNOW_LINK_PHY_RATE
#define ESPNOW_LINK_PHY_RATE WIFI_PHY_RATE_11M_L
#endif
#define RX_QUEUE_LEN        32

/* ---------------- protocol ---------------- */
#define LINK_MAGIC   0x4D564246u   /* "FBVM" */
#define LINK_VERSION 2

enum {
    T_HELLO     = 0x01,  /* sender -> broadcast, payload: hello_t */
    T_HELLO_ACK = 0x02,  /* receiver -> sender */
    T_PING      = 0x03,  /* sender -> receiver (keep-alive) */
    T_CHUNK     = 0x20,  /* sender -> receiver, payload: chunk_hdr_t + data */
    T_FRAME_ACK = 0x21,  /* receiver -> sender, payload: frame_ack_t */
};

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  role;
    uint8_t  reserved;
} hdr_t;

typedef struct __attribute__((packed)) {
    uint8_t channel;
} hello_t;

typedef struct __attribute__((packed)) {
    uint32_t frame_id;
    uint32_t total_len;
    uint32_t crc32;
    uint16_t index;
    uint16_t count;
} chunk_hdr_t;

enum { FRAME_OK = 0, FRAME_BAD_CRC = 1, FRAME_TOO_BIG = 2 };

typedef struct __attribute__((packed)) {
    uint32_t frame_id;
    uint8_t  status;
} frame_ack_t;

#define MAX_PAYLOAD  (ESP_NOW_MAX_DATA_LEN - sizeof(hdr_t))           /* 242 */
#define CHUNK_DATA   (MAX_PAYLOAD - sizeof(chunk_hdr_t))              /* 226 */
#define MAX_CHUNKS   256                                              /* 57 KB */

typedef struct {
    uint8_t mac[6];
    int     rssi;
    int     len;
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
} rx_item_t;

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ---------------- state ---------------- */
static espnow_link_config_t s_cfg;
static QueueHandle_t     s_rx_queue;
static SemaphoreHandle_t s_send_mutex, s_send_done, s_frame_done;
static volatile esp_now_send_status_t s_send_status;

static volatile bool     s_up;
static uint8_t           s_peer[6];
static volatile int64_t  s_last_ok_us;    /* sender: last ACKed send; receiver: last packet */
static uint8_t           s_channel;

/* receiver: channel the camera announced in its last HELLO. While the link is
   up (or a HELLO came in during the last second) the receiver stays on it. */
static volatile uint8_t  s_lock_ch;
static volatile int64_t  s_lock_us;
#define CHANNEL_LOCK_MS  1000

/* sender: result of the frame in flight */
static volatile uint32_t s_wait_frame_id;
static volatile uint8_t  s_frame_status;
static uint32_t          s_next_frame_id;

/* receiver: reassembly */
static uint8_t  *s_buf;
static uint32_t  s_rx_frame_id;
static uint32_t  s_rx_total, s_rx_crc;
static uint16_t  s_rx_count, s_rx_have;
static uint8_t   s_rx_seen[MAX_CHUNKS / 8];
static int64_t   s_rx_first_us;
static uint32_t  s_rx_dropped_frames;

/* ---------------- helpers ---------------- */
static int64_t now_us(void)
{
    return esp_timer_get_time();
}

static uint8_t current_channel(void)
{
    uint8_t ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&ch, &second);
    return ch;
}

static esp_err_t add_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        return ESP_OK;
    }
    esp_now_peer_info_t p = {0};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;            /* current channel */
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    return esp_now_add_peer(&p);
}

static void set_state(bool up)
{
    if (up == s_up) {
        return;
    }
    s_up = up;
    s_channel = current_channel();
    if (up) {
        ESP_LOGI(TAG, "Link UP with " MACSTR " on channel %u",
                 MAC2STR(s_peer), s_channel);
    } else {
        ESP_LOGW(TAG, "Link DOWN (nothing for %d ms)", LINK_TIMEOUT_MS);
    }
    if (s_cfg.on_state) {
        s_cfg.on_state(up, s_peer, s_channel);
    }
}

/* BikeVision 12fps plan, Phase B1 (measurement only): per-chunk time split.
   Only counted while send_frame() runs (s_tprof_on). */
static volatile bool s_tprof_on;
static int64_t s_tprof_lock_us, s_tprof_call_us, s_tprof_wait_us, s_tprof_max_us;

/* Send one packet and wait for its MAC-layer result. */
static esp_err_t raw_send(const uint8_t *mac, uint8_t type,
                          const void *payload, size_t len)
{
    uint8_t buf[ESP_NOW_MAX_DATA_LEN];
    hdr_t *h = (hdr_t *)buf;

    if (len > MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }
    h->magic = LINK_MAGIC;
    h->version = LINK_VERSION;
    h->type = type;
    h->role = (uint8_t)s_cfg.role;
    h->reserved = 0;
    if (len) {
        memcpy(buf + sizeof(*h), payload, len);
    }

    int64_t tp0 = now_us();                       /* B1 measurement */
    xSemaphoreTake(s_send_mutex, portMAX_DELAY);
    xSemaphoreTake(s_send_done, 0);
    int64_t tp1 = now_us();
    esp_err_t err = esp_now_send(mac, buf, sizeof(*h) + len);
    int64_t tp2 = now_us();
    if (err == ESP_OK) {
        if (xSemaphoreTake(s_send_done, pdMS_TO_TICKS(SEND_TIMEOUT_MS)) != pdTRUE) {
            err = ESP_ERR_TIMEOUT;
        } else if (s_send_status != ESP_NOW_SEND_SUCCESS) {
            err = ESP_FAIL;
        }
    }
    int64_t tp3 = now_us();
    xSemaphoreGive(s_send_mutex);
    if (s_tprof_on && type == T_CHUNK) {          /* B1 measurement */
        s_tprof_lock_us += tp1 - tp0;
        s_tprof_call_us += tp2 - tp1;
        s_tprof_wait_us += tp3 - tp2;
        if (tp3 - tp0 > s_tprof_max_us) {
            s_tprof_max_us = tp3 - tp0;
        }
    }

    if (err == ESP_OK && memcmp(mac, BROADCAST, 6) != 0) {
        s_last_ok_us = now_us();
    }
    return err;
}

/* ---------------- Wi-Fi callbacks (keep short) ---------------- */
static void on_send(const uint8_t *mac, esp_now_send_status_t status)
{
    s_send_status = status;
    xSemaphoreGive(s_send_done);
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len < (int)sizeof(hdr_t) || len > ESP_NOW_MAX_DATA_LEN) {
        return;
    }
    rx_item_t it;
    memcpy(it.mac, info->src_addr, 6);
    it.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    it.len = len;
    memcpy(it.data, data, len);
    xQueueSend(s_rx_queue, &it, 0);
}

/* ---------------- receiver: frame reassembly ---------------- */
static void rx_reset(uint32_t frame_id, uint32_t total, uint32_t crc, uint16_t count)
{
    s_rx_frame_id = frame_id;
    s_rx_total = total;
    s_rx_crc = crc;
    s_rx_count = count;
    s_rx_have = 0;
    memset(s_rx_seen, 0, sizeof(s_rx_seen));
    s_rx_first_us = now_us();
}

static void rx_chunk(const uint8_t *p, size_t plen)
{
    if (plen < sizeof(chunk_hdr_t)) {
        return;
    }
    const chunk_hdr_t *c = (const chunk_hdr_t *)p;
    const uint8_t *data = p + sizeof(*c);
    size_t dlen = plen - sizeof(*c);

    if (c->frame_id != s_rx_frame_id) {
        if (s_rx_frame_id != 0 && s_rx_have != 0 && s_rx_have < s_rx_count) {
            s_rx_dropped_frames++;
            ESP_LOGW(TAG, "Frame %lu incomplete (%u/%u chunks), dropped",
                     (unsigned long)s_rx_frame_id, s_rx_have, s_rx_count);
        }
        if (c->total_len > s_cfg.max_frame_size || c->count == 0 ||
            c->count > MAX_CHUNKS) {
            frame_ack_t a = { c->frame_id, FRAME_TOO_BIG };
            raw_send(s_peer, T_FRAME_ACK, &a, sizeof(a));
            ESP_LOGW(TAG, "Frame %lu too big (%lu B, buffer %u B)",
                     (unsigned long)c->frame_id, (unsigned long)c->total_len,
                     (unsigned)s_cfg.max_frame_size);
            s_rx_frame_id = c->frame_id;
            s_rx_have = s_rx_count = 0;
            return;
        }
        rx_reset(c->frame_id, c->total_len, c->crc32, c->count);
    }
    if (s_rx_count == 0 || c->index >= s_rx_count) {
        return;
    }

    uint32_t off = (uint32_t)c->index * CHUNK_DATA;
    if (off + dlen > s_rx_total) {
        return;
    }
    uint8_t bit = 1u << (c->index % 8);
    if (s_rx_seen[c->index / 8] & bit) {
        return;  /* duplicate (retry after a lost ACK) */
    }
    memcpy(s_buf + off, data, dlen);
    s_rx_seen[c->index / 8] |= bit;
    s_rx_have++;

    if (s_rx_have < s_rx_count) {
        return;
    }

    /* Complete frame */
    uint32_t rx_ms = (uint32_t)((now_us() - s_rx_first_us) / 1000);
    uint32_t crc = esp_rom_crc32_le(0, s_buf, s_rx_total);
    uint8_t status = (crc == s_rx_crc) ? FRAME_OK : FRAME_BAD_CRC;

    if (status == FRAME_OK) {
        ESP_LOGI(TAG, "Frame %lu: %lu B in %u chunks, CRC OK, %lu ms",
                 (unsigned long)s_rx_frame_id, (unsigned long)s_rx_total,
                 s_rx_count, (unsigned long)rx_ms);
        if (s_cfg.on_frame) {
            s_cfg.on_frame(s_buf, s_rx_total, s_rx_frame_id);
        }
    } else {
        ESP_LOGW(TAG, "Frame %lu: CRC mismatch, dropped",
                 (unsigned long)s_rx_frame_id);
    }

    frame_ack_t a = { s_rx_frame_id, status };
    raw_send(s_peer, T_FRAME_ACK, &a, sizeof(a));
    s_rx_count = 0;  /* buffer free for the next frame */
}

/* ---------------- receive task (both roles) ---------------- */
static void rx_task(void *arg)
{
    rx_item_t it;
    while (true) {
        if (xQueueReceive(s_rx_queue, &it, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        const hdr_t *h = (const hdr_t *)it.data;
        if (h->magic != LINK_MAGIC || h->version != LINK_VERSION ||
            h->role == (uint8_t)s_cfg.role) {
            continue;
        }
        const uint8_t *p = it.data + sizeof(*h);
        size_t plen = it.len - sizeof(*h);

        if (s_cfg.role == ESPNOW_LINK_RECEIVER) {
            if (h->type == T_HELLO) {
                /* Lock onto the camera's channel before answering, so the
                   scanner cannot hop away between HELLO and link-up. */
                if (s_cfg.channel == 0 && plen >= sizeof(hello_t)) {
                    uint8_t ch = ((const hello_t *)p)->channel;
                    if (ch >= 1 && ch <= 13) {
                        s_lock_ch = ch;
                        s_lock_us = now_us();
                        if (current_channel() != ch) {
                            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
                        }
                    }
                }
                if (memcmp(s_peer, it.mac, 6) != 0) {
                    if (esp_now_is_peer_exist(s_peer)) {
                        esp_now_del_peer(s_peer);
                    }
                    memcpy(s_peer, it.mac, 6);
                }
                add_peer(it.mac);
                s_last_ok_us = now_us();
                esp_err_t e = raw_send(it.mac, T_HELLO_ACK, NULL, 0);
                if (!s_up) {
                    ESP_LOGI(TAG, "HELLO from " MACSTR " (rssi %d) -> ACK %s",
                             MAC2STR(it.mac), it.rssi,
                             e == ESP_OK ? "delivered" : "failed");
                }
                set_state(true);
                continue;
            }
            if (!s_up || memcmp(s_peer, it.mac, 6) != 0) {
                continue;
            }
            s_last_ok_us = now_us();
            if (h->type == T_CHUNK) {
                rx_chunk(p, plen);
            }
            /* T_PING: just refreshes s_last_ok_us */
        } else { /* SENDER */
            if (h->type == T_HELLO_ACK) {
                if (memcmp(s_peer, it.mac, 6) != 0) {
                    if (esp_now_is_peer_exist(s_peer)) {
                        esp_now_del_peer(s_peer);
                    }
                    memcpy(s_peer, it.mac, 6);
                }
                if (add_peer(it.mac) == ESP_OK) {
                    s_last_ok_us = now_us();
                    set_state(true);
                }
            } else if (h->type == T_FRAME_ACK && s_up &&
                       memcmp(s_peer, it.mac, 6) == 0 &&
                       plen >= sizeof(frame_ack_t)) {
                const frame_ack_t *a = (const frame_ack_t *)p;
                if (a->frame_id == s_wait_frame_id) {
                    s_frame_status = a->status;
                    xSemaphoreGive(s_frame_done);
                }
            }
        }
    }
}

/* ---------------- link supervision task ---------------- */
static void link_task(void *arg)
{
    int64_t last_ping = 0, last_hop = 0;
    uint8_t scan_ch = 1;
    uint32_t n = 0;

    while (true) {
        int64_t now = now_us();

        if (s_up && now - s_last_ok_us > (int64_t)LINK_TIMEOUT_MS * 1000) {
            set_state(false);
        }

        if (s_cfg.role == ESPNOW_LINK_SENDER) {
            if (!s_up) {
                hello_t hl = { current_channel() };
                raw_send(BROADCAST, T_HELLO, &hl, sizeof(hl));
                if ((n++ % 25) == 0) {
                    ESP_LOGI(TAG, "Looking for display on channel %u "
                                  "(HELLO to FF:FF:FF:FF:FF:FF)...", hl.channel);
                }
                vTaskDelay(pdMS_TO_TICKS(HELLO_INTERVAL_MS));
                continue;
            }
            n = 0;
            if (now - last_ping > (int64_t)PING_INTERVAL_MS * 1000 &&
                now - s_last_ok_us > (int64_t)PING_INTERVAL_MS * 1000) {
                raw_send(s_peer, T_PING, NULL, 0);
                last_ping = now;
            }
        } else if (s_cfg.channel == 0) { /* RECEIVER with channel scan */
            bool locked = s_lock_ch != 0 &&
                          (s_up || now - s_lock_us < (int64_t)CHANNEL_LOCK_MS * 1000);
            if (locked) {
                /* Stay on the camera's channel; undo a hop that raced a HELLO. */
                if (current_channel() != s_lock_ch) {
                    esp_wifi_set_channel(s_lock_ch, WIFI_SECOND_CHAN_NONE);
                    ESP_LOGW(TAG, "Back to camera channel %u", s_lock_ch);
                }
            } else if (now - last_hop > (int64_t)SCAN_DWELL_MS * 1000) {
                esp_wifi_set_channel(scan_ch, WIFI_SECOND_CHAN_NONE);
                if (scan_ch == 1 && (n++ % 5) == 0) {
                    ESP_LOGI(TAG, "Scanning channels 1-13 for the camera...");
                }
                scan_ch = (scan_ch % 13) + 1;
                last_hop = now;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ---------------- public API ---------------- */
esp_err_t espnow_link_radio_start(uint8_t channel)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    /* B7: faster ESP-NOW radio rate (see ESPNOW_LINK_PHY_RATE). Not fatal:
       if the call is refused, the link keeps the 1 Mbps default and the log
       says so. */
    esp_err_t rate_err = esp_wifi_config_espnow_rate(WIFI_IF_STA, ESPNOW_LINK_PHY_RATE);
    if (rate_err == ESP_OK) {
        ESP_LOGI(TAG, "ESP-NOW rate set (phy rate %d)", (int)ESPNOW_LINK_PHY_RATE);
    } else {
        ESP_LOGW(TAG, "ESP-NOW rate not set (%s), using 1 Mbps default",
                 esp_err_to_name(rate_err));
    }
    if (channel >= 1 && channel <= 13) {
        ESP_ERROR_CHECK(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));
    }
    return ESP_OK;
}

esp_err_t espnow_link_start(const espnow_link_config_t *cfg)
{
    if (!cfg || (cfg->role != ESPNOW_LINK_SENDER && cfg->role != ESPNOW_LINK_RECEIVER)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;

    if (s_cfg.role == ESPNOW_LINK_RECEIVER) {
        if (s_cfg.max_frame_size == 0) {
            s_cfg.max_frame_size = 40 * 1024;
        }
        if (s_cfg.max_frame_size > (size_t)MAX_CHUNKS * CHUNK_DATA) {
            s_cfg.max_frame_size = (size_t)MAX_CHUNKS * CHUNK_DATA;
        }
        s_buf = malloc(s_cfg.max_frame_size);
        if (!s_buf) {
            ESP_LOGE(TAG, "No RAM for %u-byte frame buffer",
                     (unsigned)s_cfg.max_frame_size);
            return ESP_ERR_NO_MEM;
        }
        if (s_cfg.channel >= 1 && s_cfg.channel <= 13) {
            esp_wifi_set_channel(s_cfg.channel, WIFI_SECOND_CHAN_NONE);
        }
    }

    s_rx_queue = xQueueCreate(RX_QUEUE_LEN, sizeof(rx_item_t));
    s_send_mutex = xSemaphoreCreateMutex();
    s_send_done = xSemaphoreCreateBinary();
    s_frame_done = xSemaphoreCreateBinary();
    if (!s_rx_queue || !s_send_mutex || !s_send_done || !s_frame_done) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_send));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    ESP_ERROR_CHECK(add_peer(BROADCAST));

    uint8_t own[6];
    esp_wifi_get_mac(WIFI_IF_STA, own);
    ESP_LOGI(TAG, "%s started, own MAC " MACSTR "%s",
             s_cfg.role == ESPNOW_LINK_SENDER ? "SENDER" : "RECEIVER",
             MAC2STR(own),
             (s_cfg.role == ESPNOW_LINK_RECEIVER && s_cfg.channel == 0)
                 ? ", channel scan on" : "");
    if (s_cfg.role == ESPNOW_LINK_RECEIVER) {
        ESP_LOGI(TAG, "Frame buffer %u B in RAM (reused, nothing stored in flash)",
                 (unsigned)s_cfg.max_frame_size);
    }

    xTaskCreate(rx_task, "espnow_rx", 4096 + 2048, NULL, 6, NULL);
    xTaskCreate(link_task, "espnow_link", 3072, NULL, 5, NULL);
    return ESP_OK;
}

bool espnow_link_is_up(void)
{
    return s_up;
}

esp_err_t espnow_link_send_frame(const uint8_t *data, size_t len,
                                 uint32_t timeout_ms,
                                 espnow_link_frame_stats_t *stats)
{
    if (s_cfg.role != ESPNOW_LINK_SENDER || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_up) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t count = (len + CHUNK_DATA - 1) / CHUNK_DATA;
    if (count > MAX_CHUNKS) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t pkt[MAX_PAYLOAD];
    chunk_hdr_t *c = (chunk_hdr_t *)pkt;
    c->frame_id = ++s_next_frame_id;
    if (c->frame_id == 0) {
        c->frame_id = ++s_next_frame_id;  /* 0 means "none" on the receiver */
    }
    if (stats) {
        /* fill the id now so a failed frame is logged with its real number */
        memset(stats, 0, sizeof(*stats));
        stats->frame_id = c->frame_id;
        stats->bytes = len;
    }
    c->total_len = len;
    c->crc32 = esp_rom_crc32_le(0, data, len);
    c->count = (uint16_t)count;

    s_wait_frame_id = c->frame_id;
    xSemaphoreTake(s_frame_done, 0);

    uint16_t retries = 0;
    s_tprof_lock_us = s_tprof_call_us = s_tprof_wait_us = s_tprof_max_us = 0;
    s_tprof_on = true;                            /* B1 measurement */
    int64_t t0 = now_us();
    for (uint32_t i = 0; i < count; i++) {
        size_t off = i * CHUNK_DATA;
        size_t n = (len - off < CHUNK_DATA) ? (len - off) : CHUNK_DATA;
        c->index = (uint16_t)i;
        memcpy(pkt + sizeof(*c), data + off, n);

        esp_err_t err = ESP_FAIL;
        for (int r = 0; r <= CHUNK_RETRIES && s_up; r++) {
            err = raw_send(s_peer, T_CHUNK, pkt, sizeof(*c) + n);
            if (err == ESP_OK) {
                break;
            }
            retries++;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Frame %lu: chunk %lu/%lu not delivered",
                     (unsigned long)c->frame_id, (unsigned long)i + 1,
                     (unsigned long)count);
            s_tprof_on = false;
            return ESP_FAIL;
        }
    }
    uint32_t send_ms = (uint32_t)((now_us() - t0) / 1000);
    s_tprof_on = false;

    esp_err_t result = ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(s_frame_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        result = (s_frame_status == FRAME_OK) ? ESP_OK : ESP_FAIL;
    }

    if (stats) {
        stats->frame_id = c->frame_id;
        stats->bytes = len;
        stats->chunks = (uint16_t)count;
        stats->retries = retries;
        stats->send_ms = send_ms;
        stats->lock_us = (uint32_t)s_tprof_lock_us;   /* B1 measurement */
        stats->call_us = (uint32_t)s_tprof_call_us;
        stats->wait_us = (uint32_t)s_tprof_wait_us;
        stats->max_chunk_us = (uint32_t)s_tprof_max_us;
        stats->total_ms = (uint32_t)((now_us() - t0) / 1000);
    }
    return result;
}