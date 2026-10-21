/*
 * bv_gps.h - GPS (u-blox NEO-6M) on the display board.
 *
 * Wiring (claude/display-wiring.md): GPS TX -> GPIO16 (UART2 RX),
 * GPS RX <- GPIO17 (UART2 TX, optional), VCC 5 V (module has its own
 * regulator) or 3.3 V, GND. 9600 baud NMEA.
 *
 * A task reads the NMEA sentences ($GPRMC/$GNRMC + $GPGGA/$GNGGA), checks
 * the checksum, and keeps the latest values plus the trip numbers.
 * bv_gps_get() copies them out (safe from any task).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BV_GPS_TZ_MIN   330      /* local time offset: India = UTC + 5:30 */

typedef struct {
    /* time + date, already converted to local time (BV_GPS_TZ_MIN) */
    bool     time_valid;
    uint8_t  hh, mm, ss;
    bool     date_valid;
    uint8_t  day, mon;
    uint16_t year;

    /* position */
    bool     fix;               /* RMC status A */
    double   lat, lon;          /* degrees, + = N / E */
    float    speed_kmh;
    bool     course_valid;      /* only while moving */
    float    course_deg;        /* 0 = north, clockwise */
    uint8_t  sats;              /* satellites used (GGA) */
    float    hdop;
    float    alt_m;

    /* trip (counted while moving >= 3 km/h) */
    float    trip_km;
    float    max_kmh;
    uint32_t moving_s;

    /* health */
    uint32_t sentences;         /* good sentences */
    uint32_t bad_checksum;
    int64_t  last_rx_us;        /* esp_timer time of the last good sentence, 0 = never */
} bv_gps_t;

esp_err_t bv_gps_start(void);
void      bv_gps_get(bv_gps_t *out);
void      bv_gps_trip_reset(void);

/* helpers for the pages */
double bv_gps_distance_m(double lat1, double lon1, double lat2, double lon2);
double bv_gps_bearing_deg(double lat1, double lon1, double lat2, double lon2);

#ifdef __cplusplus
}
#endif
