/*
 * ota_secret.example.h - template for the camera update network (H1.3).
 *
 * Copy this file to ota_secret.h (same folder) and change the password.
 * ota_secret.h is in .gitignore: the password never goes to GitHub.
 */
#pragma once

#define OTA_AP_SSID   "BikeVision-CAM-OTA"   /* only exists while update mode is on */
#define OTA_AP_PASS   "change-me-8plus"      /* WPA2, at least 8 characters */
