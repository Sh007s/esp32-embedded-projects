/*
 * wifi_sta - join one Wi-Fi network (router or phone hotspot) as a station.
 *
 * Behaviour:
 *   - 5 quick retries, then a retry every 5 s forever (never gives up)
 *   - Wi-Fi power save off
 *
 * For ESP-NOW: let both boards join the same network so they always share
 * its channel, even when the access point changes channel.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise NVS, netif, event loop and Wi-Fi, then start connecting.
 * Returns immediately; use wifi_sta_wait_connected() to wait for an IP. */
esp_err_t wifi_sta_start(const char *ssid, const char *password);

/* Block until the station has an IP address, or the timeout expires. */
bool wifi_sta_wait_connected(TickType_t timeout);

/* True while the station has an IP address. */
bool wifi_sta_is_connected(void);

/* Current primary Wi-Fi channel (0 if unknown). */
uint8_t wifi_sta_channel(void);

#ifdef __cplusplus
}
#endif
