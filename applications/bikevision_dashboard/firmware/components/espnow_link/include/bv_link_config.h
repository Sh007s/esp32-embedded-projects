/*
 * bv_link_config.h - BikeVision camera -> display link selection.
 *
 * Shared by camera_node and display_node (both use ../components).
 * Change BV_LINK_MODE here, then rebuild and flash BOTH boards.
 *
 *   BV_LINK_ESPNOW (0): ESP-NOW, direct by MAC address (espnow_link.c)
 *   BV_LINK_WIFI   (1): Wi-Fi direct - camera's own hidden network,
 *                       display joins it, frames over UDP (wifi_link.c)
 *
 * Both files implement the same API (espnow_link.h), so the camera and
 * display code do not change between the modes.
 */
#pragma once

#define BV_LINK_ESPNOW   0
#define BV_LINK_WIFI     1

#ifndef BV_LINK_MODE
#define BV_LINK_MODE     BV_LINK_WIFI
#endif

#if BV_LINK_MODE == BV_LINK_WIFI
#define BV_LINK_MODE_NAME "Wi-Fi direct"
#else
#define BV_LINK_MODE_NAME "ESP-NOW"
#endif

/* Wi-Fi direct settings (mode 1). SSID / password: bv_link_secret.h */
#define BV_WIFI_CHANNEL    1      /* fixed channel of the camera network */
#define BV_UDP_DISP_PORT   5000   /* display listens here (frame chunks, PING) */
#define BV_UDP_CAM_PORT    5001   /* camera listens here (HELLO, DONE) */
