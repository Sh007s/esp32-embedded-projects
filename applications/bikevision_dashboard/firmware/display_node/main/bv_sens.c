/*
 * bv_sens.c - I2C sensors: HW-611 (BMP280 / BME280) + GY-271 compass.
 * See bv_sens.h for wiring and behaviour.
 */
#include "bv_sens.h"
#include "bv_gps.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "sens";

#define I2C_PORT        I2C_NUM_0
#define I2C_SDA         32
#define I2C_SCL         33
#define I2C_HZ          100000
#define I2C_TIMEOUT_MS  20

#define READ_MS         200          /* both sensors read 5 times a second */
#define LOG_PERIOD_US   (30 * 1000000LL)
#define RETRY_US        (5 * 1000000LL)  /* sensor missing: look again every 5 s */

#define SEA_LEVEL_HPA   1013.25f
#define GPS_REF_HOLD_S  60           /* good GPS fix this long -> set sea-level pressure */
#define CAL_SECONDS     20

/* Compass direction on the board. If turning the board clockwise (seen
   from above) makes the heading go DOWN, set BV_MAG_REVERSE 1.
   BV_MAG_OFFSET_DEG turns the result (mounting angle on the bike). */
#define BV_MAG_REVERSE     0
#define BV_MAG_OFFSET_DEG  0.0f

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bv_sens_t    s_v;              /* shared copy (under s_lock) */
static volatile bool s_cal_req = false;

/* ------------------------------------------------------------------ */
/* I2C helpers                                                          */
/* ------------------------------------------------------------------ */

static esp_err_t rd(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_write_read_device(I2C_PORT, addr, &reg, 1, buf, n,
                                        pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t wr(uint8_t addr, uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_write_to_device(I2C_PORT, addr, b, 2, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static bool probe(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

static void scan(bv_sens_t *v)
{
    char line[96];
    int n = 0;
    v->n_found = 0;
    line[0] = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (probe(a)) {
            if (v->n_found < sizeof(v->found)) {
                v->found[v->n_found++] = a;
            }
            n += snprintf(line + n, sizeof(line) - n, " 0x%02X", a);
            if (n >= (int)sizeof(line) - 6) break;
        }
    }
    if (v->n_found) {
        ESP_LOGI(TAG, "I2C scan (SDA %d, SCL %d): found%s", I2C_SDA, I2C_SCL, line);
    } else {
        ESP_LOGW(TAG, "I2C scan (SDA %d, SCL %d): nothing found - check wiring / 3.3 V",
                 I2C_SDA, I2C_SCL);
    }
}

/* ------------------------------------------------------------------ */
/* BMP280 / BME280                                                      */
/* ------------------------------------------------------------------ */

static struct {
    uint16_t T1; int16_t T2, T3;
    uint16_t P1; int16_t P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t  H1, H3; int16_t H2, H4, H5; int8_t H6;
} cal;

static bool baro_init(bv_sens_t *v)
{
    static const uint8_t ADDR[2] = { 0x76, 0x77 };
    for (int i = 0; i < 2; i++) {
        uint8_t id = 0;
        if (rd(ADDR[i], 0xD0, &id, 1) != ESP_OK) continue;
        if (id != 0x58 && id != 0x60) {
            ESP_LOGW(TAG, "0x%02X answers but chip id 0x%02X is not BMP280/BME280", ADDR[i], id);
            continue;
        }
        uint8_t c[24];
        if (rd(ADDR[i], 0x88, c, 24) != ESP_OK) continue;
        cal.T1 = c[0] | c[1] << 8;   cal.T2 = (int16_t)(c[2] | c[3] << 8);
        cal.T3 = (int16_t)(c[4] | c[5] << 8);
        cal.P1 = c[6] | c[7] << 8;
        cal.P2 = (int16_t)(c[8] | c[9] << 8);    cal.P3 = (int16_t)(c[10] | c[11] << 8);
        cal.P4 = (int16_t)(c[12] | c[13] << 8);  cal.P5 = (int16_t)(c[14] | c[15] << 8);
        cal.P6 = (int16_t)(c[16] | c[17] << 8);  cal.P7 = (int16_t)(c[18] | c[19] << 8);
        cal.P8 = (int16_t)(c[20] | c[21] << 8);  cal.P9 = (int16_t)(c[22] | c[23] << 8);

        v->baro = (id == 0x60) ? BV_BARO_BME280 : BV_BARO_BMP280;
        v->baro_addr = ADDR[i];
        if (v->baro == BV_BARO_BME280) {
            uint8_t h[7];
            rd(ADDR[i], 0xA1, &cal.H1, 1);
            rd(ADDR[i], 0xE1, h, 7);
            cal.H2 = (int16_t)(h[0] | h[1] << 8);
            cal.H3 = h[2];
            cal.H4 = (int16_t)((int8_t)h[3] * 16 | (h[4] & 0x0F));
            cal.H5 = (int16_t)((int8_t)h[5] * 16 | (h[4] >> 4));
            cal.H6 = (int8_t)h[6];
            wr(ADDR[i], 0xF2, 0x01);             /* humidity x1 (before ctrl_meas) */
        }
        wr(ADDR[i], 0xF5, 0x28);                 /* standby 62.5 ms, filter x4 */
        wr(ADDR[i], 0xF4, 0x57);                 /* temp x2, pressure x16, normal mode */
        ESP_LOGI(TAG, "Pressure sensor: %s (chip id 0x%02X) at 0x%02X%s",
                 bv_sens_baro_name(v->baro), id, ADDR[i],
                 v->baro == BV_BARO_BMP280 ? " - no humidity on this chip" : "");
        return true;
    }
    return false;
}

/* why the last read failed (for the log) */
static const char *s_baro_why = "";

static bool baro_read(bv_sens_t *v, float *t_c, float *p_hpa, float *h_pct)
{
    uint8_t d[8];
    size_t n = (v->baro == BV_BARO_BME280) ? 8 : 6;
    if (rd(v->baro_addr, 0xF7, d, n) != ESP_OK) { s_baro_why = "no I2C answer (loose wire?)"; return false; }
    int32_t adc_p = (int32_t)d[0] << 12 | d[1] << 4 | d[2] >> 4;
    int32_t adc_t = (int32_t)d[3] << 12 | d[4] << 4 | d[5] >> 4;
    if (adc_t == 0x80000 || adc_p == 0x80000) {
        /* answers, but not measuring: the chip restarted (power dip) and is in sleep mode */
        s_baro_why = "chip restarted (power dip?), not measuring";
        return false;
    }
    s_baro_why = "values out of range";

    double v1 = (adc_t / 16384.0 - cal.T1 / 1024.0) * cal.T2;
    double v2 = (adc_t / 131072.0 - cal.T1 / 8192.0);
    v2 = v2 * v2 * cal.T3;
    double t_fine = v1 + v2;
    *t_c = (float)(t_fine / 5120.0);

    v1 = t_fine / 2.0 - 64000.0;
    v2 = v1 * v1 * cal.P6 / 32768.0;
    v2 = v2 + v1 * cal.P5 * 2.0;
    v2 = v2 / 4.0 + cal.P4 * 65536.0;
    v1 = (cal.P3 * v1 * v1 / 524288.0 + cal.P2 * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * cal.P1;
    if (v1 == 0) return false;
    double p = 1048576.0 - adc_p;
    p = (p - v2 / 4096.0) * 6250.0 / v1;
    v1 = cal.P9 * p * p / 2147483648.0;
    v2 = p * cal.P8 / 32768.0;
    p = p + (v1 + v2 + cal.P7) / 16.0;
    *p_hpa = (float)(p / 100.0);

    *h_pct = -1.0f;
    if (v->baro == BV_BARO_BME280) {
        int32_t adc_h = d[6] << 8 | d[7];
        double h = t_fine - 76800.0;
        h = (adc_h - (cal.H4 * 64.0 + cal.H5 / 16384.0 * h)) *
            (cal.H2 / 65536.0 * (1.0 + cal.H6 / 67108864.0 * h * (1.0 + cal.H3 / 67108864.0 * h)));
        h = h * (1.0 - cal.H1 * h / 524288.0);
        if (h > 100.0) h = 100.0;
        if (h < 0.0) h = 0.0;
        *h_pct = (float)h;
    }
    return *p_hpa > 300.0f && *p_hpa < 1100.0f;
}

/* ------------------------------------------------------------------ */
/* Compass: QMC5883L (0x0D), HMC5883L (0x1E), QMC5883P (0x2C)            */
/* ------------------------------------------------------------------ */

static bool mag_init(bv_sens_t *v)
{
    uint8_t b[3];
    if (rd(0x0D, 0x0D, b, 1) == ESP_OK && b[0] == 0xFF) {
        wr(0x0D, 0x0B, 0x01);                    /* set/reset period */
        wr(0x0D, 0x09, 0x1D);                    /* continuous, 200 Hz, 8 G, OSR 512 */
        v->mag = BV_MAG_QMC5883L; v->mag_addr = 0x0D;
    } else if (rd(0x1E, 0x0A, b, 3) == ESP_OK && b[0] == 'H' && b[1] == '4' && b[2] == '3') {
        wr(0x1E, 0x00, 0x70);                    /* 8 samples averaged, 15 Hz */
        wr(0x1E, 0x01, 0x20);                    /* +-1.3 G */
        wr(0x1E, 0x02, 0x00);                    /* continuous */
        v->mag = BV_MAG_HMC5883L; v->mag_addr = 0x1E;
    } else if (rd(0x2C, 0x00, b, 1) == ESP_OK && b[0] == 0x80) {
        wr(0x2C, 0x29, 0x06);                    /* axis signs */
        wr(0x2C, 0x0B, 0x08);                    /* set/reset on, 8 G */
        wr(0x2C, 0x0A, 0xCD);                    /* normal mode, 200 Hz */
        v->mag = BV_MAG_QMC5883P; v->mag_addr = 0x2C;
    } else {
        return false;
    }
    ESP_LOGI(TAG, "Compass: %s at 0x%02X", bv_sens_mag_name(v->mag), v->mag_addr);
    return true;
}

static bool mag_read(bv_sens_t *v, int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t d[6];
    switch (v->mag) {
    case BV_MAG_QMC5883L:
        if (rd(v->mag_addr, 0x00, d, 6) != ESP_OK) return false;
        *x = (int16_t)(d[0] | d[1] << 8); *y = (int16_t)(d[2] | d[3] << 8); *z = (int16_t)(d[4] | d[5] << 8);
        return true;
    case BV_MAG_QMC5883P:
        if (rd(v->mag_addr, 0x01, d, 6) != ESP_OK) return false;
        *x = (int16_t)(d[0] | d[1] << 8); *y = (int16_t)(d[2] | d[3] << 8); *z = (int16_t)(d[4] | d[5] << 8);
        return true;
    case BV_MAG_HMC5883L:
        if (rd(v->mag_addr, 0x03, d, 6) != ESP_OK) return false;
        *x = (int16_t)(d[0] << 8 | d[1]); *z = (int16_t)(d[2] << 8 | d[3]); *y = (int16_t)(d[4] << 8 | d[5]);
        return true;
    default:
        return false;
    }
}

/* calibration: centre + half range of x and y (kept in NVS) */
static struct { float ox, oy, rx, ry; } s_mcal = { 0, 0, 1, 1 };

static void mcal_load(bv_sens_t *v)
{
    nvs_handle_t h;
    size_t n = sizeof(s_mcal);
    if (nvs_open("bv_sens", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "magcal", &s_mcal, &n) == ESP_OK && n == sizeof(s_mcal)) {
            v->mag_cal = true;
            ESP_LOGI(TAG, "Compass calibration loaded: centre %.0f / %.0f, range %.0f / %.0f",
                     s_mcal.ox, s_mcal.oy, s_mcal.rx, s_mcal.ry);
        }
        nvs_close(h);
    }
}

static void mcal_save(void)
{
    nvs_handle_t h;
    if (nvs_open("bv_sens", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "magcal", &s_mcal, sizeof(s_mcal));
        nvs_commit(h);
        nvs_close(h);
    }
}

static float heading(int16_t mx, int16_t my)
{
    float x = (mx - s_mcal.ox) / s_mcal.rx;
    float y = (my - s_mcal.oy) / s_mcal.ry;
    float h = atan2f(y, x) * 180.0f / (float)M_PI;
    if (BV_MAG_REVERSE) h = -h;
    h += BV_MAG_OFFSET_DEG;
    while (h < 0) h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;
    return h;
}

/* ------------------------------------------------------------------ */
/* Task                                                                 */
/* ------------------------------------------------------------------ */

static void publish(const bv_sens_t *v)
{
    portENTER_CRITICAL(&s_lock);
    s_v = *v;
    portEXIT_CRITICAL(&s_lock);
}

static void sens_task(void *arg)
{
    bv_sens_t v;
    memset(&v, 0, sizeof(v));
    v.hum_pct = -1.0f;
    scan(&v);
    baro_init(&v);
    if (mag_init(&v)) mcal_load(&v);
    if (v.baro == BV_BARO_NONE) ESP_LOGW(TAG, "No BMP280/BME280 found (0x76 / 0x77)");
    if (v.mag == BV_MAG_NONE)   ESP_LOGW(TAG, "No compass found (0x0D / 0x1E / 0x2C)");
    publish(&v);

    float   p0 = SEA_LEVEL_HPA;
    float   alt_s = 0;                 /* smoothed altitude */
    bool    alt_init = false;
    float   ring[20];                  /* altitude once a second, last 20 s */
    int     ring_n = 0, ring_i = 0;
    float   asc_base = 0;
    int     gps_good_s = 0;
    int64_t last_sec = esp_timer_get_time(), last_log = last_sec, last_retry = last_sec;
    int64_t cal_end = 0;
    float   cmin[2] = { 0 }, cmax[2] = { 0 };
    uint32_t baro_fail = 0, mag_fail = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(READ_MS));
        int64_t now = esp_timer_get_time();

        /* sensors missing: look again (wires can be plugged in while running) */
        if ((v.baro == BV_BARO_NONE || v.mag == BV_MAG_NONE) && now - last_retry > RETRY_US) {
            last_retry = now;
            if (v.baro == BV_BARO_NONE && baro_init(&v)) alt_init = false;
            if (v.mag == BV_MAG_NONE && mag_init(&v)) mcal_load(&v);
        }

        /* pressure + temperature (+ humidity) */
        if (v.baro != BV_BARO_NONE) {
            float t, p, h;
            if (baro_read(&v, &t, &p, &h)) {
                baro_fail = 0;
                v.baro_ok = true;
                v.temp_c = t;
                v.press_hpa = p;
                v.hum_pct = h;
                float a = 44330.0f * (1.0f - powf(p / p0, 0.1903f));
                alt_s = alt_init ? alt_s + 0.1f * (a - alt_s) : a;
                if (!alt_init) { asc_base = alt_s; ring_n = 0; }
                alt_init = true;
                v.alt_m = alt_s;
            } else if (++baro_fail >= 10) {
                /* 2 s of failed reads: log why, set the chip up again (every 2 s until it works) */
                v.read_errors++;
                if (v.baro_ok) ESP_LOGW(TAG, "Pressure sensor failed: %s - setting it up again", s_baro_why);
                v.baro_ok = false;
                baro_fail = 0;
                bv_baro_kind_t was = v.baro;
                if (baro_init(&v)) {
                    ESP_LOGI(TAG, "Pressure sensor set up again (errors so far %lu)", (unsigned long)v.read_errors);
                } else {
                    v.baro = was;           /* keep trying on the same chip */
                }
            }
        }

        /* compass */
        if (v.mag != BV_MAG_NONE) {
            int16_t x, y, z;
            if (mag_read(&v, &x, &y, &z)) {
                mag_fail = 0;
                v.mag_ok = true;
                v.mx = x; v.my = y; v.mz = z;
                if (s_cal_req) {
                    s_cal_req = false;
                    cal_end = now + CAL_SECONDS * 1000000LL;
                    cmin[0] = cmax[0] = x;
                    cmin[1] = cmax[1] = y;
                    ESP_LOGI(TAG, "Compass calibration started: turn the board slowly in all directions (%d s)", CAL_SECONDS);
                }
                if (cal_end) {
                    if (x < cmin[0]) cmin[0] = x;
                    if (x > cmax[0]) cmax[0] = x;
                    if (y < cmin[1]) cmin[1] = y;
                    if (y > cmax[1]) cmax[1] = y;
                    v.cal_running = true;
                    v.cal_left_s = (uint8_t)((cal_end - now) / 1000000 + 1);
                    if (now >= cal_end) {
                        cal_end = 0;
                        v.cal_running = false;
                        v.cal_left_s = 0;
                        float rx = (cmax[0] - cmin[0]) / 2, ry = (cmax[1] - cmin[1]) / 2;
                        if (rx > 50 && ry > 50) {
                            s_mcal.ox = (cmax[0] + cmin[0]) / 2;
                            s_mcal.oy = (cmax[1] + cmin[1]) / 2;
                            s_mcal.rx = rx;
                            s_mcal.ry = ry;
                            mcal_save();
                            v.mag_cal = true;
                            ESP_LOGI(TAG, "Compass calibration saved: centre %.0f / %.0f, range %.0f / %.0f",
                                     s_mcal.ox, s_mcal.oy, rx, ry);
                        } else {
                            ESP_LOGW(TAG, "Compass calibration FAILED: board not turned enough "
                                          "(range %.0f / %.0f) - not saved", rx, ry);
                        }
                    }
                }
                v.heading_deg = heading(x, y);
            } else if (++mag_fail >= 10) {
                if (v.mag_ok) ESP_LOGW(TAG, "Compass not answering");
                v.mag_ok = false;
                v.read_errors++;
                mag_fail = 0;
            }
        }

        /* once a second: climb rate, total climb, GPS reference */
        if (now - last_sec >= 1000000) {
            last_sec = now;
            if (alt_init && v.baro_ok) {
                float old = ring_n == 20 ? ring[ring_i] : (ring_n ? ring[0] : alt_s);
                int span = ring_n ? ring_n : 1;
                ring[ring_i] = alt_s;
                ring_i = (ring_i + 1) % 20;
                if (ring_n < 20) ring_n++;
                v.climb_m_min = (alt_s - old) * 60.0f / (float)span;
                if (alt_s > asc_base + 2.0f) { v.ascent_m += alt_s - asc_base; asc_base = alt_s; }
                else if (alt_s < asc_base - 2.0f) { asc_base = alt_s; }

                bv_gps_t g;
                bv_gps_get(&g);
                bool good = g.fix && g.sats >= 6 && g.hdop > 0 && g.hdop < 2.0f;
                gps_good_s = good ? gps_good_s + 1 : 0;
                if (!v.alt_from_gps && gps_good_s >= GPS_REF_HOLD_S) {
                    p0 = v.press_hpa / powf(1.0f - g.alt_m / 44330.0f, 5.255f);
                    v.alt_from_gps = true;
                    alt_init = false;          /* restart smoothing at the new height */
                    ESP_LOGI(TAG, "Altitude set from GPS: %.0f m (sea-level pressure %.2f hPa)", g.alt_m, p0);
                }
            }
        }

        if (now - last_log >= LOG_PERIOD_US) {
            last_log = now;
            if (v.baro != BV_BARO_NONE) {
                char hum[16] = "";
                if (v.hum_pct >= 0) snprintf(hum, sizeof(hum), " %.0f %%RH", v.hum_pct);
                ESP_LOGI(TAG, "%s: %.1f C%s %.2f hPa, altitude %.0f m (%s), climb %+.1f m/min, total climb %.0f m%s",
                         bv_sens_baro_name(v.baro), v.temp_c, hum, v.press_hpa, v.alt_m,
                         v.alt_from_gps ? "GPS ref" : "standard 1013 hPa",
                         v.climb_m_min, v.ascent_m, v.baro_ok ? "" : " - NOT ANSWERING");
            }
            if (v.mag != BV_MAG_NONE) {
                ESP_LOGI(TAG, "%s: heading %.0f, raw x %d y %d z %d, calibrated %s%s",
                         bv_sens_mag_name(v.mag), v.heading_deg, v.mx, v.my, v.mz,
                         v.mag_cal ? "yes" : "NO (hold knob on SENSORS)",
                         v.mag_ok ? "" : " - NOT ANSWERING");
            }
        }
        publish(&v);
    }
}

/* ------------------------------------------------------------------ */

esp_err_t bv_sens_start(void)
{
    i2c_config_t c = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,   /* boards have their own too */
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_PORT, &c);
    if (err == ESP_OK) err = i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C start failed: %s", esp_err_to_name(err));
        return err;
    }
    memset(&s_v, 0, sizeof(s_v));
    s_v.hum_pct = -1.0f;
    /* low priority, core 0 (the display task does the drawing) */
    if (xTaskCreatePinnedToCore(sens_task, "sens", 4096, NULL, 3, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void bv_sens_get(bv_sens_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_v;
    portEXIT_CRITICAL(&s_lock);
}

void bv_sens_cal_start(void)
{
    s_cal_req = true;
}

const char *bv_sens_baro_name(bv_baro_kind_t k)
{
    return k == BV_BARO_BME280 ? "BME280" : k == BV_BARO_BMP280 ? "BMP280" : "none";
}

const char *bv_sens_mag_name(bv_mag_kind_t k)
{
    return k == BV_MAG_QMC5883L ? "QMC5883L" : k == BV_MAG_HMC5883L ? "HMC5883L" :
           k == BV_MAG_QMC5883P ? "QMC5883P" : "none";
}
