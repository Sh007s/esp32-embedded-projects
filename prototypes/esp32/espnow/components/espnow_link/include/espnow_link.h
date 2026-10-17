/*
 * espnow_link - reusable direct ESP32-to-ESP32 link over ESP-NOW (one pair).
 *
 * What it does:
 *   - Auto-pairing: the INITIATOR broadcasts HELLO every 500 ms until the
 *     RESPONDER answers with HELLO_ACK. Both sides add the other as a unicast peer and
 *     save its MAC in NVS. Re-flashing or replacing either board re-pairs
 *     automatically.
 *   - Reliable-ish send: espnow_link_send() waits for the MAC-layer ACK of each
 *     unicast packet and returns ESP_OK only if the peer received it.
 *   - Receive: packets are copied out of the Wi-Fi task into a queue and handed
 *     to your callback from a separate task, so your callback may take time.
 *
 * Requirements:
 *   - Wi-Fi must be started (esp_wifi_start) before espnow_link_start().
 *   - Both boards must be on the same Wi-Fi channel. Easiest: both join the
 *     same Wi-Fi network (see wifi_sta component).
 *
 * Packet format (max 250 bytes, ESP-NOW limit on IDF v5.1):
 *   [espnow_link_hdr_t 8 bytes][payload up to ESPNOW_LINK_MAX_PAYLOAD bytes]
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESPNOW_LINK_MAGIC      0x4B4C4E45u /* "ENLK" little-endian */
#define ESPNOW_LINK_VERSION    1

typedef struct __attribute__((packed)) {
    uint32_t magic;    /* ESPNOW_LINK_MAGIC */
    uint8_t  version;  /* ESPNOW_LINK_VERSION */
    uint8_t  type;     /* espnow_link_type_t or an application type */
    uint8_t  role;     /* espnow_link_role_t of the sender */
    uint8_t  reserved;
} espnow_link_hdr_t;

#define ESPNOW_LINK_MAX_PAYLOAD (ESP_NOW_MAX_DATA_LEN - sizeof(espnow_link_hdr_t))

typedef enum {
    ESPNOW_LINK_ROLE_INITIATOR  = 1,
    ESPNOW_LINK_ROLE_RESPONDER = 2,
} espnow_link_role_t;

/* Types below 0x10 are reserved for the link itself. */
typedef enum {
    ESPNOW_LINK_TYPE_HELLO     = 0x01,
    ESPNOW_LINK_TYPE_HELLO_ACK = 0x02,
    ESPNOW_LINK_TYPE_APP_FIRST = 0x10, /* first type free for applications */
} espnow_link_type_t;

/* Called from the link's receive task (not the Wi-Fi task) for every
 * application packet (type >= ESPNOW_LINK_TYPE_APP_FIRST) from the peer. */
typedef void (*espnow_link_rx_cb_t)(uint8_t type, const uint8_t *payload,
                                    size_t len, int rssi);

typedef struct {
    uint32_t sent_ok;      /* unicast packets ACKed by the peer */
    uint32_t sent_fail;    /* unicast packets not ACKed / timed out */
    uint32_t rx_packets;   /* application packets delivered to the callback */
    uint32_t rx_dropped;   /* packets dropped because the RX queue was full */
    uint32_t rx_invalid;   /* wrong magic/version/length, or not from the peer */
} espnow_link_stats_t;

/* Start ESP-NOW and pairing. Call once, after Wi-Fi is started. */
esp_err_t espnow_link_start(espnow_link_role_t role, espnow_link_rx_cb_t rx_cb);

/* Wait until paired with the other board. */
bool espnow_link_wait_paired(TickType_t timeout);
bool espnow_link_is_paired(void);

/* Send one packet to the paired peer and wait for its MAC-layer ACK.
 * len must be <= ESPNOW_LINK_MAX_PAYLOAD.
 * Returns ESP_OK if the peer ACKed, ESP_FAIL if not, ESP_ERR_TIMEOUT if no
 * send result arrived in time, ESP_ERR_INVALID_STATE if not paired. */
esp_err_t espnow_link_send(uint8_t type, const void *payload, size_t len,
                           TickType_t timeout);

void espnow_link_get_peer(uint8_t mac[6]);
void espnow_link_get_stats(espnow_link_stats_t *out);
void espnow_link_reset_stats(void);

#ifdef __cplusplus
}
#endif
