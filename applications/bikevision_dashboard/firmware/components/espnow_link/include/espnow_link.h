/*
 * espnow_link - camera -> display frame link over ESP-NOW (one pair).
 *
 * SENDER (camera board)
 *   - Uses whatever Wi-Fi channel the board is already on (e.g. the phone
 *     hotspot the web page uses). Call after Wi-Fi is started.
 *   - Not paired: broadcasts HELLO to FF:FF:FF:FF:FF:FF every 200 ms.
 *   - Paired: sends a PING every second; link is "down" again if nothing is
 *     ACKed for 3 s (e.g. display off, or the hotspot moved channel).
 *   - espnow_link_send_frame(): splits a JPEG into 226-byte chunks, sends
 *     each with MAC-layer ACK (+ up to 3 retries), then waits until the
 *     display reports the frame done. So only one frame is in flight and the
 *     camera always sends a fresh frame next.
 *
 * RECEIVER (display board)
 *   - Does not join any network. espnow_link_radio_start() starts Wi-Fi
 *     only as a radio.
 *   - Not paired: scans channels 1..13 (300 ms each) until it hears a
 *     HELLO, then stays on that channel and answers HELLO_ACK.
 *   - Reassembles chunks into ONE reusable RAM buffer (max_frame_size),
 *     checks CRC32, calls on_frame(), then tells the sender it is done.
 *     Nothing is stored in flash; the buffer is overwritten by the next frame.
 *   - Link "down" if nothing heard for 3 s -> scans again.
 *
 * ESP-IDF v5.1.x, target esp32.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESPNOW_LINK_SENDER   = 1,
    ESPNOW_LINK_RECEIVER = 2,
} espnow_link_role_t;

/* Called from the link's own task when a complete, CRC-checked frame is in
 * the buffer. The buffer is only valid until this returns. Drawing on the
 * TFT inside this callback is fine: the sender waits for it to finish. */
typedef void (*espnow_link_frame_cb_t)(const uint8_t *data, size_t len,
                                       uint32_t frame_id);

/* Called when the link goes up or down. */
typedef void (*espnow_link_state_cb_t)(bool up, const uint8_t mac[6],
                                       uint8_t channel);

typedef struct {
    espnow_link_role_t     role;
    /* RECEIVER: 0 = scan channels 1..13 (recommended), 1..13 = fixed.
     * SENDER: ignored (uses the board's current Wi-Fi channel). */
    uint8_t                channel;
    /* RECEIVER: size of the one reusable frame buffer (e.g. 40 * 1024). */
    size_t                 max_frame_size;
    espnow_link_frame_cb_t on_frame;   /* RECEIVER only, may be NULL */
    espnow_link_state_cb_t on_state;   /* optional, both roles */
} espnow_link_config_t;

typedef struct {
    uint32_t frame_id;
    uint32_t bytes;
    uint16_t chunks;
    uint16_t retries;      /* chunk re-sends needed */
    uint32_t send_ms;      /* first chunk to last chunk ACKed */
    uint32_t total_ms;     /* first chunk to display "done" */
} espnow_link_frame_stats_t;

/* RECEIVER helper: NVS + event loop + Wi-Fi as a radio only (no network). */
esp_err_t espnow_link_radio_start(uint8_t channel);

/* Start the link. Wi-Fi must already be started. */
esp_err_t espnow_link_start(const espnow_link_config_t *cfg);

bool espnow_link_is_up(void);

/* SENDER only. Blocks until the display reports the frame done, or
 * timeout_ms passes. Returns ESP_OK if the display received it intact.
 * stats may be NULL. */
esp_err_t espnow_link_send_frame(const uint8_t *data, size_t len,
                                 uint32_t timeout_ms,
                                 espnow_link_frame_stats_t *stats);

#ifdef __cplusplus
}
#endif
