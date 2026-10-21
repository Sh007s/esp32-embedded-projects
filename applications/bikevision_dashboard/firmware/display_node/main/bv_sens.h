/*
 * bv_sens.h - I2C sensors on the display board (shared bus, GPIO32/33).
 *
 * Wiring (claude/display-wiring.md): SDA -> GPIO32, SCL -> GPIO33, 3.3 V, GND.
 *   HW-611 (BMP280 / BME280): VCC 3.3 V, CSB -> 3.3 V (I2C mode),
 *                             SDO -> GND (address 0x76; 3.3 V = 0x77)
 *   GY-271 (HW-246) compass:  VCC 3.3 V, DRDY not used
 *                             (QMC5883L 0x0D, HMC5883L 0x1E or QMC5883P 0x2C)
 *
 * At start the bus is scanned and every address found is logged, so the
 * chips can be identified. A task reads both sensors every 200 ms and keeps
 * the latest values; bv_sens_get() copies them out (safe from any task).
 *
 * Altitude from pressure: sea-level pressure starts at the standard
 * 1013.25 hPa and is set once from the GPS altitude when the GPS has had a
 * good fix for 60 s (then the barometer follows height changes to ~1 m).
 *
 * Compass: needs a calibration once (turn the board slowly in all
 * directions for 20 s); the result is kept in NVS (bv_sens / magcal).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { BV_BARO_NONE = 0, BV_BARO_BMP280, BV_BARO_BME280 } bv_baro_kind_t;
typedef enum { BV_MAG_NONE = 0, BV_MAG_QMC5883L, BV_MAG_HMC5883L, BV_MAG_QMC5883P } bv_mag_kind_t;

typedef struct {
    /* pressure sensor */
    bv_baro_kind_t baro;
    uint8_t  baro_addr;
    bool     baro_ok;           /* last read worked */
    float    temp_c;
    float    hum_pct;           /* BME280 only, else < 0 */
    float    press_hpa;
    float    alt_m;             /* from pressure */
    bool     alt_from_gps;      /* sea-level pressure set from the GPS */
    float    climb_m_min;       /* + = going up (over the last ~20 s) */
    float    ascent_m;          /* total climb since power-on */

    /* compass */
    bv_mag_kind_t mag;
    uint8_t  mag_addr;
    bool     mag_ok;
    int16_t  mx, my, mz;        /* raw */
    float    heading_deg;       /* 0 = north (board X arrow), clockwise */
    bool     mag_cal;           /* calibration saved */
    bool     cal_running;
    uint8_t  cal_left_s;

    /* bus */
    uint8_t  found[8];          /* addresses answering the scan */
    uint8_t  n_found;
    uint32_t read_errors;
} bv_sens_t;

esp_err_t   bv_sens_start(void);          /* call after NVS is started */
void        bv_sens_get(bv_sens_t *out);
void        bv_sens_cal_start(void);       /* compass calibration, 20 s */
const char *bv_sens_baro_name(bv_baro_kind_t k);
const char *bv_sens_mag_name(bv_mag_kind_t k);

#ifdef __cplusplus
}
#endif
