#ifndef ESPNOW_CAM_H
#define ESPNOW_CAM_H

#include "esp_err.h"

/* Start sending camera JPEG frames to the display board over ESP-NOW.
 * Call after wifi_init() (the link uses the hotspot's channel). */
esp_err_t espnow_cam_start(void);

#endif
