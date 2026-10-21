/*
 * bv_gps.c - NEO-6M NMEA reader (UART2, 9600 baud). See bv_gps.h.
 */
#include "bv_gps.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "gps";

#define GPS_UART        UART_NUM_2
#define GPS_RX_GPIO     16      /* GPS TX -> ESP32 RX */
#define GPS_TX_GPIO     17      /* ESP32 TX -> GPS RX (not used yet) */
#define GPS_BAUD        9600
#define GPS_LINE_MAX    100

#define MOVING_KMH      3.0f    /* below this the trip and course are not updated */
#define MAX_JUMP_M      150.0   /* ignore position jumps larger than this per second */

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bv_gps_t s_gps;

/* trip state (gps task only) */
static bool   s_have_last;
static double s_last_lat, s_last_lon;
static int64_t s_last_move_us;

/* ---------------- helpers ---------------- */

double bv_gps_distance_m(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371000.0, d2r = M_PI / 180.0;
    double dlat = (lat2 - lat1) * d2r, dlon = (lon2 - lon1) * d2r;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * d2r) * cos(lat2 * d2r) * sin(dlon / 2) * sin(dlon / 2);
    return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}

double bv_gps_bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    const double d2r = M_PI / 180.0;
    double y = sin((lon2 - lon1) * d2r) * cos(lat2 * d2r);
    double x = cos(lat1 * d2r) * sin(lat2 * d2r) -
               sin(lat1 * d2r) * cos(lat2 * d2r) * cos((lon2 - lon1) * d2r);
    double b = atan2(y, x) / d2r;
    return b < 0 ? b + 360.0 : b;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* "$....*hh": XOR of everything between $ and * must equal hh */
static bool checksum_ok(const char *line)
{
    if (line[0] != '$') return false;
    uint8_t x = 0;
    const char *p = line + 1;
    while (*p && *p != '*') x ^= (uint8_t)*p++;
    if (*p != '*' || hexval(p[1]) < 0 || hexval(p[2]) < 0) return false;
    return x == (uint8_t)((hexval(p[1]) << 4) | hexval(p[2]));
}

/* split at commas (in place, stops at '*'); returns field count */
static int split(char *s, char **f, int max)
{
    int n = 0;
    char *star = strchr(s, '*');
    if (star) *star = 0;
    f[n++] = s;
    while (*s && n < max) {
        if (*s == ',') { *s = 0; f[n++] = s + 1; }
        s++;
    }
    return n;
}

/* "ddmm.mmmm" + hemisphere -> degrees */
static bool parse_coord(const char *v, const char *hemi, int deg_digits, double *out)
{
    if (!v[0] || !hemi[0] || (int)strlen(v) < deg_digits + 2) return false;
    char d[4] = {0};
    memcpy(d, v, deg_digits);
    double deg = atof(d) + atof(v + deg_digits) / 60.0;
    if (hemi[0] == 'S' || hemi[0] == 'W') deg = -deg;
    *out = deg;
    return true;
}

static int days_in_month(int m, int y)
{
    static const int D[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return D[(m - 1) % 12];
}

/* UTC -> local (adds BV_GPS_TZ_MIN, carries into the date) */
static void to_local(int *hh, int *mm, int *day, int *mon, int *year)
{
    int t = *hh * 60 + *mm + BV_GPS_TZ_MIN;
    int dd = 0;
    while (t >= 1440) { t -= 1440; dd++; }
    while (t < 0)     { t += 1440; dd--; }
    *hh = t / 60;
    *mm = t % 60;
    if (dd > 0 && *day > 0) {
        (*day)++;
        if (*day > days_in_month(*mon, *year)) {
            *day = 1;
            if (++(*mon) > 12) { *mon = 1; (*year)++; }
        }
    } else if (dd < 0 && *day > 0) {
        if (--(*day) < 1) {
            if (--(*mon) < 1) { *mon = 12; (*year)--; }
            *day = days_in_month(*mon, *year);
        }
    }
}

/* ---------------- sentences ---------------- */

/* $xxRMC,hhmmss.ss,A,lat,N,lon,E,knots,course,ddmmyy,... */
static void handle_rmc(char **f, int n)
{
    if (n < 10) return;
    bv_gps_t g;
    portENTER_CRITICAL(&s_mux);
    g = s_gps;
    portEXIT_CRITICAL(&s_mux);

    int hh = -1, mm = 0, ss = 0, day = 0, mon = 0, year = 0;
    if (strlen(f[1]) >= 6) {
        hh = (f[1][0] - '0') * 10 + (f[1][1] - '0');
        mm = (f[1][2] - '0') * 10 + (f[1][3] - '0');
        ss = (f[1][4] - '0') * 10 + (f[1][5] - '0');
    }
    if (strlen(f[9]) >= 6) {
        day = (f[9][0] - '0') * 10 + (f[9][1] - '0');
        mon = (f[9][2] - '0') * 10 + (f[9][3] - '0');
        year = 2000 + (f[9][4] - '0') * 10 + (f[9][5] - '0');
    }
    if (hh >= 0 && hh < 24 && mm < 60 && ss < 61) {
        bool date_ok = day >= 1 && mon >= 1 && mon <= 12;
        to_local(&hh, &mm, date_ok ? &day : &day, &mon, &year);
        g.time_valid = true;
        g.hh = (uint8_t)hh; g.mm = (uint8_t)mm; g.ss = (uint8_t)ss;
        if (date_ok) {
            g.date_valid = true;
            g.day = (uint8_t)day; g.mon = (uint8_t)mon; g.year = (uint16_t)year;
        }
    }

    double lat, lon;
    g.fix = (f[2][0] == 'A') && parse_coord(f[3], f[4], 2, &lat) && parse_coord(f[5], f[6], 3, &lon);
    if (g.fix) {
        g.lat = lat;
        g.lon = lon;
        g.speed_kmh = (float)(atof(f[7]) * 1.852);
        bool moving = g.speed_kmh >= MOVING_KMH;
        g.course_valid = moving && f[8][0];
        if (g.course_valid) g.course_deg = (float)atof(f[8]);

        int64_t now = esp_timer_get_time();
        if (moving) {
            if (s_have_last) {
                double d = bv_gps_distance_m(s_last_lat, s_last_lon, lat, lon);
                if (d < MAX_JUMP_M) g.trip_km += (float)(d / 1000.0);
            }
            if (s_last_move_us) {
                int64_t dt = (now - s_last_move_us) / 1000000;
                if (dt >= 1 && dt <= 3) g.moving_s += (uint32_t)dt;
            }
            s_last_move_us = now;
            if (g.speed_kmh > g.max_kmh) g.max_kmh = g.speed_kmh;
        } else {
            s_last_move_us = 0;
        }
        s_have_last = true;
        s_last_lat = lat;
        s_last_lon = lon;
    } else {
        g.speed_kmh = 0;
        g.course_valid = false;
        s_have_last = false;
        s_last_move_us = 0;
    }

    portENTER_CRITICAL(&s_mux);
    /* keep fields the GGA handler owns */
    g.sats = s_gps.sats; g.hdop = s_gps.hdop; g.alt_m = s_gps.alt_m;
    g.sentences = s_gps.sentences; g.bad_checksum = s_gps.bad_checksum; g.last_rx_us = s_gps.last_rx_us;
    s_gps = g;
    portEXIT_CRITICAL(&s_mux);
}

/* $xxGGA,time,lat,N,lon,E,quality,sats,hdop,alt,M,... */
static void handle_gga(char **f, int n)
{
    if (n < 10) return;
    uint8_t sats = (uint8_t)atoi(f[7]);
    float hdop = f[8][0] ? (float)atof(f[8]) : 99.0f;
    float alt = (float)atof(f[9]);
    portENTER_CRITICAL(&s_mux);
    s_gps.sats = sats;
    s_gps.hdop = hdop;
    if (f[6][0] && f[6][0] != '0') s_gps.alt_m = alt;
    portEXIT_CRITICAL(&s_mux);
}

static void handle_line(char *line)
{
    if (!checksum_ok(line)) {
        portENTER_CRITICAL(&s_mux);
        s_gps.bad_checksum++;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    char *f[22];
    int n = split(line, f, 22);
    const char *t = f[0];
    if (strlen(t) == 6 && strcmp(t + 3, "RMC") == 0) {
        handle_rmc(f, n);
    } else if (strlen(t) == 6 && strcmp(t + 3, "GGA") == 0) {
        handle_gga(f, n);
    }
    portENTER_CRITICAL(&s_mux);
    s_gps.sentences++;
    s_gps.last_rx_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_mux);
}

static void gps_task(void *arg)
{
    static char line[GPS_LINE_MAX + 1];
    int len = 0;
    uint8_t buf[128];
    int64_t last_log = 0;
    while (true) {
        int n = uart_read_bytes(GPS_UART, buf, sizeof(buf), pdMS_TO_TICKS(200));
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '$') {
                len = 0;
            }
            if (c == '\r' || c == '\n') {
                if (len > 6) {
                    line[len] = 0;
                    handle_line(line);
                }
                len = 0;
            } else if (len < GPS_LINE_MAX) {
                line[len++] = c;
            } else {
                len = 0;            /* too long: drop */
            }
        }
        int64_t now = esp_timer_get_time();
        if (now - last_log > 30000000) {       /* one status line every 30 s */
            last_log = now;
            bv_gps_t g;
            bv_gps_get(&g);
            ESP_LOGI(TAG, "GPS: %lu sentences (%lu bad), fix %s, %u sats, %02u:%02u:%02u, %.1f km/h",
                     (unsigned long)g.sentences, (unsigned long)g.bad_checksum,
                     g.fix ? "YES" : "no", g.sats, g.hh, g.mm, g.ss, g.speed_kmh);
        }
    }
}

/* ---------------- API ---------------- */

esp_err_t bv_gps_start(void)
{
    uart_config_t cfg = {
        .baud_rate = GPS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(GPS_UART, 2048, 0, 0, NULL, 0);
    if (err != ESP_OK) return err;
    ESP_ERROR_CHECK(uart_param_config(GPS_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART, GPS_TX_GPIO, GPS_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    memset(&s_gps, 0, sizeof(s_gps));
    s_gps.hdop = 99.0f;
    /* low priority, core 0 (input and GPS stay off the picture core) */
    xTaskCreatePinnedToCore(gps_task, "gps", 4096, NULL, 3, NULL, 0);
    ESP_LOGI(TAG, "GPS on UART2: RX GPIO%d (from GPS TX), TX GPIO%d, %d baud, local time UTC%+d:%02d",
             GPS_RX_GPIO, GPS_TX_GPIO, GPS_BAUD, BV_GPS_TZ_MIN / 60, abs(BV_GPS_TZ_MIN % 60));
    return ESP_OK;
}

void bv_gps_get(bv_gps_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_gps;
    portEXIT_CRITICAL(&s_mux);
}

void bv_gps_trip_reset(void)
{
    portENTER_CRITICAL(&s_mux);
    s_gps.trip_km = 0;
    s_gps.max_kmh = 0;
    s_gps.moving_s = 0;
    portEXIT_CRITICAL(&s_mux);
}
