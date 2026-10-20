/*
 * wifi_link.c - BikeVision camera -> display link over Wi-Fi direct (E1).
 *
 * Same API as espnow_link.h. Compiled only when BV_LINK_MODE == BV_LINK_WIFI
 * (include/bv_link_config.h); otherwise this file is empty.
 *
 * SENDER (camera board)
 *   - Starts its own hidden network (SoftAP): SSID / password from
 *     bv_link_secret.h, WPA2, fixed channel BV_WIFI_CHANNEL, max 1 client.
 *     No router, no hotspot, no internet. Camera IP 192.168.4.1.
 *   - Listens on UDP BV_UDP_CAM_PORT for HELLO (display alive) and DONE
 *     (display finished a frame). Sends PING every 500 ms.
 *   - espnow_link_send_frame(): splits the JPEG into chunks of up to
 *     WL_CHUNK_MAX bytes, sends them back to back over UDP, then waits for
 *     DONE. Only one frame in flight (same as ESP-NOW).
 *
 * RECEIVER (display board)
 *   - Joins the camera network as a station (automatic, nothing typed),
 *     gets 192.168.4.x by DHCP, reconnects by itself after a camera reset.
 *   - Sends HELLO to the camera every 500 ms.
 *   - Reassembles chunks into ONE reusable RAM buffer (max_frame_size),
 *     checks CRC32, calls on_frame(), then sends DONE.
 *
 * Link "up" = heard the other board within the last 3 s.
 * Wi-Fi power save is off on both boards (low delay).
 *
 * ESP-IDF v5.1.x, target esp32.
 */
#include "bv_link_config.h"

#if BV_LINK_MODE == BV_LINK_WIFI

#if defined(__has_include)
#if __has_include("bv_link_secret.h")
#include "bv_link_secret.h"
#endif
#endif
#ifndef BV_WIFI_SSID
#error "Wi-Fi direct: copy include/bv_link_secret.example.h to include/bv_link_secret.h and set BV_WIFI_SSID / BV_WIFI_PASS"
#endif

#include "espnow_link.h"

#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_rom_crc.h"
#include "esp_rom_sys.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char *TAG = "wifi_link";

/* ---------------------------------------------------------------- */
/* Packet format                                                     */
/* ---------------------------------------------------------------- */

#define WL_MAGIC        0x4256      /* "BV" */
#define WL_CHUNK_MAX    1400        /* payload per UDP packet (fits 1500 B MTU) */
#define WL_MAX_CHUNKS   32          /* 32 x 1400 = 44 KB max frame */
#define WL_LINK_TIMEOUT_US   (3000 * 1000)
#define WL_KEEPALIVE_MS      500
#define WL_REJOIN_US         (5000 * 1000)   /* display: no reply this long -> rejoin network */

enum {
    WL_HELLO = 1,   /* display -> camera, every 500 ms */
    WL_PING  = 2,   /* camera -> display, every 500 ms */
    WL_CHUNK = 3,   /* camera -> display, frame data */
    WL_DONE  = 4,   /* display -> camera, frame finished (ok = CRC good) */
};

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  type;
    uint8_t  ok;          /* DONE: 1 = CRC ok */
    uint32_t frame_id;
    uint32_t total_len;
    uint32_t crc;
    uint32_t offset;
    uint16_t idx;
    uint16_t count;
} wl_hdr_t;              /* 24 bytes */

typedef struct {
    uint32_t frame_id;
    uint8_t  ok;
} wl_done_t;

/* ---------------------------------------------------------------- */
/* State                                                             */
/* ---------------------------------------------------------------- */

static espnow_link_config_t s_cfg;
static volatile bool    s_up;
static volatile int64_t s_last_heard_us;
static uint8_t          s_peer_mac[6];      /* camera: display MAC; display: camera AP MAC */

static int s_rx_sock = -1;
static int s_tx_sock = -1;
static struct sockaddr_in s_peer_addr;      /* where to send */
static volatile bool s_peer_known;

/* SENDER */
static QueueHandle_t s_done_q;
static uint32_t      s_frame_id;
static uint8_t       s_txbuf[sizeof(wl_hdr_t) + WL_CHUNK_MAX];

/* RECEIVER */
static volatile bool s_have_ip;
static volatile int64_t s_ip_time_us;      /* when the display got its IP */
static uint8_t  *s_buf;
static uint32_t  s_rx_fid, s_rx_total, s_rx_crc, s_rx_mask, s_last_done_fid;
static uint16_t  s_rx_count;

/* ---------------------------------------------------------------- */
/* Helpers                                                           */
/* ---------------------------------------------------------------- */

static void set_link(bool up)
{
    if (up == s_up) {
        return;
    }
    s_up = up;
    if (up) {
        ESP_LOGI(TAG, "Link UP with " MACSTR " (%s, channel %d)",
                 MAC2STR(s_peer_mac), BV_LINK_MODE_NAME, BV_WIFI_CHANNEL);
    } else {
        ESP_LOGW(TAG, "Link DOWN (nothing heard for 3000 ms)");
    }
    if (s_cfg.on_state) {
        s_cfg.on_state(up, s_peer_mac, BV_WIFI_CHANNEL);
    }
}

static void heard_peer(void)
{
    s_last_heard_us = esp_timer_get_time();
    set_link(true);
}

static int send_small(uint8_t type, uint32_t frame_id, uint8_t ok)
{
    if (!s_peer_known) {
        return -1;
    }
    wl_hdr_t h = {0};
    h.magic = WL_MAGIC;
    h.type = type;
    h.ok = ok;
    h.frame_id = frame_id;
    struct sockaddr_in to = s_peer_addr;
    return sendto(s_tx_sock, &h, sizeof(h), 0, (struct sockaddr *)&to, sizeof(to));
}

static int open_rx_socket(uint16_t port)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        return -1;
    }
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
        ESP_LOGE(TAG, "bind(%u) failed: errno %d", port, errno);
        close(s);
        return -1;
    }
    return s;
}

/* Keep-alive + link timeout, both roles. */
static void keepalive_task(void *arg)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(WL_KEEPALIVE_MS));

        if (s_cfg.role == ESPNOW_LINK_SENDER) {
            send_small(WL_PING, 0, 0);
        } else if (s_have_ip) {
            send_small(WL_HELLO, 0, 0);
        }

        if (s_up && esp_timer_get_time() - s_last_heard_us > WL_LINK_TIMEOUT_US) {
            set_link(false);
        }

        /* Display only: joined the network but nothing heard from the camera
           for 5 s (e.g. camera restarted and no longer knows us, while its
           beacons keep coming) -> leave and join again. The DISCONNECTED
           event below reconnects. This is what a display reset did by hand. */
        if (s_cfg.role == ESPNOW_LINK_RECEIVER && s_have_ip) {
            int64_t now = esp_timer_get_time();
            int64_t last = s_last_heard_us > s_ip_time_us ? s_last_heard_us : s_ip_time_us;
            if (now - last > WL_REJOIN_US) {
                ESP_LOGW(TAG, "No reply from the camera for 5 s - rejoining the camera network");
                s_have_ip = false;
                set_link(false);
                esp_wifi_disconnect();
            }
        }
    }
}

/* ---------------------------------------------------------------- */
/* SENDER (camera)                                                   */
/* ---------------------------------------------------------------- */

static void sender_rx_task(void *arg)
{
    uint8_t pkt[64];
    while (true) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(s_rx_sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &fl);
        if (n < (int)sizeof(wl_hdr_t)) {
            continue;
        }
        wl_hdr_t h;
        memcpy(&h, pkt, sizeof(h));
        if (h.magic != WL_MAGIC) {
            continue;
        }

        if (h.type == WL_HELLO || h.type == WL_DONE) {
            if (!s_peer_known || s_peer_addr.sin_addr.s_addr != from.sin_addr.s_addr) {
                struct sockaddr_in to = {0};
                to.sin_family = AF_INET;
                to.sin_port = htons(BV_UDP_DISP_PORT);
                to.sin_addr = from.sin_addr;
                s_peer_addr = to;
                s_peer_known = true;
                ESP_LOGI(TAG, "Display at %s", inet_ntoa(from.sin_addr));
            }
            heard_peer();
        }

        if (h.type == WL_DONE) {
            wl_done_t d = { .frame_id = h.frame_id, .ok = h.ok };
            xQueueSend(s_done_q, &d, 0);
        }
    }
}

static void sender_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)data;
        memcpy(s_peer_mac, e->mac, 6);
        ESP_LOGI(TAG, "Display joined the camera network: " MACSTR, MAC2STR(e->mac));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGW(TAG, "Display left the camera network: " MACSTR, MAC2STR(e->mac));
        s_peer_known = false;
        set_link(false);
    } else if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        ip_event_ap_staipassigned_t *e = (ip_event_ap_staipassigned_t *)data;
        ESP_LOGI(TAG, "Display got IP " IPSTR, IP2STR(&e->ip));
    }
}

static esp_err_t start_sender(void)
{
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        sender_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED,
                                                        sender_event, NULL, NULL));

    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, BV_WIFI_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(BV_WIFI_SSID);
    strncpy((char *)ap.ap.password, BV_WIFI_PASS, sizeof(ap.ap.password));
    ap.ap.channel = BV_WIFI_CHANNEL;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.ssid_hidden = 1;
    ap.ap.max_connection = 1;
    ap.ap.beacon_interval = 100;
    ap.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    ESP_LOGI(TAG, "SENDER: camera network started (hidden), AP MAC " MACSTR
                  ", channel %d, IP 192.168.4.1, max 1 client",
             MAC2STR(mac), BV_WIFI_CHANNEL);

    s_done_q = xQueueCreate(4, sizeof(wl_done_t));
    s_rx_sock = open_rx_socket(BV_UDP_CAM_PORT);
    s_tx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!s_done_q || s_rx_sock < 0 || s_tx_sock < 0) {
        return ESP_FAIL;
    }

    xTaskCreate(sender_rx_task, "wl_rx", 4096, NULL, 6, NULL);
    xTaskCreate(keepalive_task, "wl_keep", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "Waiting for the display to join...");
    return ESP_OK;
}

esp_err_t espnow_link_send_frame(const uint8_t *data, size_t len,
                                 uint32_t timeout_ms,
                                 espnow_link_frame_stats_t *stats)
{
    espnow_link_frame_stats_t st = {0};
    if (s_cfg.role != ESPNOW_LINK_SENDER || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_up || !s_peer_known) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t count = (uint16_t)((len + WL_CHUNK_MAX - 1) / WL_CHUNK_MAX);
    if (count > WL_MAX_CHUNKS) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t fid = ++s_frame_id;
    uint32_t crc = esp_rom_crc32_le(0, data, len);
    st.frame_id = fid;
    st.bytes = (uint32_t)len;
    st.chunks = count;

    xQueueReset(s_done_q);
    struct sockaddr_in to = s_peer_addr;
    esp_err_t result = ESP_OK;

    int64_t t0 = esp_timer_get_time();
    for (uint16_t i = 0; i < count && result == ESP_OK; i++) {
        uint32_t off = (uint32_t)i * WL_CHUNK_MAX;
        uint32_t plen = (uint32_t)len - off;
        if (plen > WL_CHUNK_MAX) {
            plen = WL_CHUNK_MAX;
        }

        wl_hdr_t h = {0};
        h.magic = WL_MAGIC;
        h.type = WL_CHUNK;
        h.frame_id = fid;
        h.total_len = (uint32_t)len;
        h.crc = crc;
        h.offset = off;
        h.idx = i;
        h.count = count;
        memcpy(s_txbuf, &h, sizeof(h));
        memcpy(s_txbuf + sizeof(h), data + off, plen);

        int64_t c0 = esp_timer_get_time();
        int tries = 0;
        while (sendto(s_tx_sock, s_txbuf, sizeof(h) + plen, 0,
                      (struct sockaddr *)&to, sizeof(to)) < 0) {
            /* ENOMEM = Wi-Fi TX buffers full for a moment: wait a little */
            if ((errno == ENOMEM || errno == EAGAIN) && tries < 50) {
                tries++;
                st.retries++;
                esp_rom_delay_us(500);
                taskYIELD();
                continue;
            }
            ESP_LOGW(TAG, "Frame %lu: chunk %u/%u send failed: errno %d",
                     (unsigned long)fid, i + 1, count, errno);
            result = ESP_FAIL;
            break;
        }
        uint32_t c_us = (uint32_t)(esp_timer_get_time() - c0);
        st.call_us += c_us;
        if (c_us > st.max_chunk_us) {
            st.max_chunk_us = c_us;
        }
    }
    int64_t t1 = esp_timer_get_time();
    st.send_ms = (uint32_t)((t1 - t0) / 1000);

    if (result == ESP_OK) {
        /* Wait for DONE of this frame (older DONEs are skipped) */
        result = ESP_ERR_TIMEOUT;
        int64_t deadline = t0 + (int64_t)timeout_ms * 1000;
        while (true) {
            int64_t left_us = deadline - esp_timer_get_time();
            if (left_us <= 0) {
                break;
            }
            wl_done_t d;
            if (xQueueReceive(s_done_q, &d, pdMS_TO_TICKS(left_us / 1000 + 1)) != pdTRUE) {
                break;
            }
            if (d.frame_id == fid) {
                result = d.ok ? ESP_OK : ESP_ERR_INVALID_CRC;
                break;
            }
        }
    }

    int64_t t2 = esp_timer_get_time();
    st.wait_us = (uint32_t)(t2 - t1);    /* whole frame: last chunk sent -> DONE */
    st.total_ms = (uint32_t)((t2 - t0) / 1000);
    if (stats) {
        *stats = st;
    }
    return result;
}

/* ---------------------------------------------------------------- */
/* RECEIVER (display)                                                */
/* ---------------------------------------------------------------- */

static void receiver_rx_task(void *arg)
{
    static uint8_t pkt[sizeof(wl_hdr_t) + WL_CHUNK_MAX + 16];
    uint32_t incomplete = 0, crc_fail = 0;

    while (true) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(s_rx_sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &fl);
        if (n < (int)sizeof(wl_hdr_t)) {
            continue;
        }
        wl_hdr_t h;
        memcpy(&h, pkt, sizeof(h));
        if (h.magic != WL_MAGIC) {
            continue;
        }
        heard_peer();

        if (h.type != WL_CHUNK) {
            continue;   /* PING */
        }

        uint32_t plen = (uint32_t)n - sizeof(wl_hdr_t);
        if (h.frame_id == s_last_done_fid ||
            h.count == 0 || h.count > WL_MAX_CHUNKS || h.idx >= h.count ||
            h.total_len > s_cfg.max_frame_size ||
            h.offset + plen > h.total_len) {
            continue;   /* late duplicate or bad packet */
        }

        if (h.frame_id != s_rx_fid) {
            if (s_rx_fid != 0 && s_rx_fid != s_last_done_fid) {
                incomplete++;
                ESP_LOGW(TAG, "Frame %lu incomplete (%d of %u chunks), dropped (total %lu)",
                         (unsigned long)s_rx_fid, __builtin_popcount(s_rx_mask),
                         s_rx_count, (unsigned long)incomplete);
            }
            s_rx_fid = h.frame_id;
            s_rx_total = h.total_len;
            s_rx_crc = h.crc;
            s_rx_count = h.count;
            s_rx_mask = 0;
        }

        memcpy(s_buf + h.offset, pkt + sizeof(wl_hdr_t), plen);
        s_rx_mask |= (1u << h.idx);

        uint32_t full = (s_rx_count == 32) ? 0xFFFFFFFFu : ((1u << s_rx_count) - 1u);
        if (s_rx_mask != full) {
            continue;
        }

        bool ok = (esp_rom_crc32_le(0, s_buf, s_rx_total) == s_rx_crc);
        if (ok) {
            if (s_cfg.on_frame) {
                s_cfg.on_frame(s_buf, s_rx_total, s_rx_fid);
            }
        } else {
            crc_fail++;
            ESP_LOGW(TAG, "Frame %lu CRC error (total %lu)",
                     (unsigned long)s_rx_fid, (unsigned long)crc_fail);
        }
        s_last_done_fid = s_rx_fid;
        send_small(WL_DONE, s_rx_fid, ok ? 1 : 0);
    }
}

static void receiver_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Looking for the camera network...");
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *e = (wifi_event_sta_connected_t *)data;
        memcpy(s_peer_mac, e->bssid, 6);
        ESP_LOGI(TAG, "Joined camera network, camera AP " MACSTR ", channel %d",
                 MAC2STR(e->bssid), e->channel);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *e = (wifi_event_sta_disconnected_t *)data;
        if (s_have_ip || s_up) {
            ESP_LOGW(TAG, "Camera network lost (reason %d), reconnecting...", e->reason);
        }
        s_have_ip = false;
        set_link(false);
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        struct sockaddr_in to = {0};
        to.sin_family = AF_INET;
        to.sin_port = htons(BV_UDP_CAM_PORT);
        to.sin_addr.s_addr = e->ip_info.gw.addr;   /* camera = gateway 192.168.4.1 */
        s_peer_addr = to;
        s_peer_known = true;
        s_ip_time_us = esp_timer_get_time();
        s_have_ip = true;
        ESP_LOGI(TAG, "Got IP " IPSTR ", camera at " IPSTR,
                 IP2STR(&e->ip_info.ip), IP2STR(&e->ip_info.gw));
        send_small(WL_HELLO, 0, 0);
    }
}

static esp_err_t start_receiver(void)
{
    s_buf = malloc(s_cfg.max_frame_size);
    if (!s_buf) {
        ESP_LOGE(TAG, "No RAM for the %u B frame buffer", (unsigned)s_cfg.max_frame_size);
        return ESP_ERR_NO_MEM;
    }

    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        receiver_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        receiver_event, NULL, NULL));

    s_rx_sock = open_rx_socket(BV_UDP_DISP_PORT);
    s_tx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_rx_sock < 0 || s_tx_sock < 0) {
        return ESP_FAIL;
    }

    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, BV_WIFI_SSID, sizeof(sta.sta.ssid));
    strncpy((char *)sta.sta.password, BV_WIFI_PASS, sizeof(sta.sta.password));
    sta.sta.channel = BV_WIFI_CHANNEL;
    sta.sta.scan_method = WIFI_FAST_SCAN;
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sta.sta.pmf_cfg.capable = true;
    sta.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));

    xTaskCreate(receiver_rx_task, "wl_rx", 4096, NULL, 6, NULL);
    xTaskCreate(keepalive_task, "wl_keep", 3072, NULL, 4, NULL);

    ESP_ERROR_CHECK(esp_wifi_start());   /* STA_START event -> connect */
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "RECEIVER: frame buffer %u B (reused), joining the camera network",
             (unsigned)s_cfg.max_frame_size);
    return ESP_OK;
}

/* ---------------------------------------------------------------- */
/* Public API (same as espnow_link.h)                                */
/* ---------------------------------------------------------------- */

esp_err_t espnow_link_radio_start(uint8_t channel)
{
    (void)channel;   /* Wi-Fi direct always uses BV_WIFI_CHANNEL */

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_LOGI(TAG, "Link mode: %s (BV_LINK_MODE %d)", BV_LINK_MODE_NAME, BV_LINK_MODE);
    return ESP_OK;
}

esp_err_t espnow_link_start(const espnow_link_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;
    if (s_cfg.role == ESPNOW_LINK_SENDER) {
        return start_sender();
    }
    if (s_cfg.role == ESPNOW_LINK_RECEIVER) {
        if (s_cfg.max_frame_size == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        return start_receiver();
    }
    return ESP_ERR_INVALID_ARG;
}

bool espnow_link_is_up(void)
{
    return s_up;
}

#endif /* BV_LINK_MODE == BV_LINK_WIFI */
