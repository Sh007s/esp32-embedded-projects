#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>

/*
 * OTA update mode (H1.3).
 *
 * Hold the update button (IO13 to GND) while the camera powers on:
 *   - the camera does NOT start the camera, SD card or display link
 *   - it opens its own update network (OTA_AP_SSID / OTA_AP_PASS, ota_secret.h)
 *   - the laptop joins that network and uploads the new firmware:
 *       curl --data-binary @build/camera_test.bin http://192.168.4.1/update
 *   - the camera writes it to the free slot, checks it, and restarts into it
 *   - the red LED on the back blinks while update mode is on
 *   - no upload within 10 minutes -> normal restart
 */

/* true if the update button is held right now (IO13 low). */
bool ota_update_button_held(void);

/* Runs update mode. Never returns (restarts the camera at the end). */
void ota_update_mode_run(void);

#endif
