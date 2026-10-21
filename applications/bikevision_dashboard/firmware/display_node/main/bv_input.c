/*
 * bv_input.c - rotary encoder (KY-040) + optional buttons for the display.
 *
 * Encoder: both CLK and DT raise an interrupt on every edge; a full-step
 * state machine reports one click only after a complete valid sequence, so
 * contact bounce never produces extra or reversed clicks.
 *
 * Push switch and buttons: polled every 5 ms, a level must be stable for
 * 20 ms to count. Short press = released before BV_INPUT_LONG_MS; long press
 * fires once when the hold time is reached.
 */
#include "bv_input.h"

#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "input";

#define BV_ENC_CLK_GPIO          GPIO_NUM_34
#define BV_ENC_DT_GPIO           GPIO_NUM_35
#define BV_ENC_SW_GPIO           GPIO_NUM_27
#define BV_BTN1_GPIO             GPIO_NUM_25
#define BV_BTN2_GPIO             GPIO_NUM_26

#define BV_INPUT_BUTTONS         0     /* 1 when the 12x12 buttons are wired */
#define BV_ENC_REVERSE           1     /* 1 if right/left come out swapped */
#define BV_INPUT_POLL_MS         10    /* >= 1 tick: FreeRTOS runs at 100 Hz (10 ms) */
#define BV_INPUT_DEBOUNCE_MS     20
#define BV_INPUT_LONG_MS         2000

static QueueHandle_t s_q;

/* ---------------- encoder (interrupt) ---------------- */

/* Full-step state machine (Ben Buxton's rotary table). It only reports a
   click after the complete valid sequence 11 -> 01 -> 00 -> 10 -> 11 (or the
   reverse). Contact bounce just moves back and forth inside the sequence and
   never produces a click, and a half-finished turn produces nothing. */
#define R_START     0x0
#define R_CW_FINAL  0x1
#define R_CW_BEGIN  0x2
#define R_CW_NEXT   0x3
#define R_CCW_BEGIN 0x4
#define R_CCW_FINAL 0x5
#define R_CCW_NEXT  0x6
#define DIR_CW      0x10
#define DIR_CCW     0x20
static const uint8_t RTABLE[7][4] = {
    /* R_START     */ { R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START },
    /* R_CW_FINAL  */ { R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW },
    /* R_CW_BEGIN  */ { R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START },
    /* R_CW_NEXT   */ { R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START },
    /* R_CCW_BEGIN */ { R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START },
    /* R_CCW_FINAL */ { R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW },
    /* R_CCW_NEXT  */ { R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START },
};
static volatile uint8_t s_rstate = R_START;
static volatile uint32_t s_edges;     /* raw edge count, for the test log */

static void IRAM_ATTR enc_isr(void *arg)
{
    s_edges++;
    uint8_t pins = (uint8_t)((gpio_get_level(BV_ENC_DT_GPIO) << 1) | gpio_get_level(BV_ENC_CLK_GPIO));
    s_rstate = RTABLE[s_rstate & 0x0f][pins];
    uint8_t d = s_rstate & 0x30;
    if (!d) {
        return;
    }
    int8_t dir = (d == DIR_CW) ? +1 : -1;
    if (BV_ENC_REVERSE) {
        dir = -dir;
    }
    bv_input_event_t ev = { .type = BV_IN_TURN, .src = BV_SRC_KNOB, .dir = dir };
    BaseType_t woken = pdFALSE;
    if (s_q) {
        xQueueSendFromISR(s_q, &ev, &woken);
    }
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

/* ---------------- push switch + buttons (polled) ---------------- */

typedef struct {
    gpio_num_t pin;
    uint8_t    src;
    bool       stable_down;   /* debounced state */
    bool       raw_last;
    int64_t    raw_since_us;
    int64_t    down_since_us;
    bool       long_sent;
} bv_key_t;

static bv_key_t s_keys[] = {
    { .pin = BV_ENC_SW_GPIO, .src = BV_SRC_KNOB },
#if BV_INPUT_BUTTONS
    { .pin = BV_BTN1_GPIO,   .src = BV_SRC_BTN1 },
    { .pin = BV_BTN2_GPIO,   .src = BV_SRC_BTN2 },
#endif
};
#define NKEYS (sizeof(s_keys) / sizeof(s_keys[0]))

static void post(uint8_t type, uint8_t src)
{
    bv_input_event_t ev = { .type = type, .src = src, .dir = 0 };
    if (s_q) {
        xQueueSend(s_q, &ev, 0);
    }
}

static void key_task(void *arg)
{
    const int64_t debounce_us = (int64_t)BV_INPUT_DEBOUNCE_MS * 1000;
    const int64_t long_us = (int64_t)BV_INPUT_LONG_MS * 1000;
    while (true) {
        int64_t now = esp_timer_get_time();
        for (size_t i = 0; i < NKEYS; i++) {
            bv_key_t *k = &s_keys[i];
            bool raw_down = gpio_get_level(k->pin) == 0;   /* to GND = pressed */
            if (raw_down != k->raw_last) {
                k->raw_last = raw_down;
                k->raw_since_us = now;
            }
            if (raw_down != k->stable_down && now - k->raw_since_us >= debounce_us) {
                k->stable_down = raw_down;
                if (raw_down) {
                    k->down_since_us = now;
                    k->long_sent = false;
                } else if (!k->long_sent) {
                    post(BV_IN_PRESS, k->src);
                }
            }
            if (k->stable_down && !k->long_sent && now - k->down_since_us >= long_us) {
                k->long_sent = true;
                post(BV_IN_LONG, k->src);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BV_INPUT_POLL_MS) > 0 ? pdMS_TO_TICKS(BV_INPUT_POLL_MS) : 1);
    }
}

/* ---------------- start ---------------- */

esp_err_t bv_input_start(QueueHandle_t q)
{
    if (!q) {
        return ESP_ERR_INVALID_ARG;
    }
    s_q = q;

    /* GPIO34/35 are input-only and have no internal pull-ups: the KY-040
       board's own 10k resistors pull them to its + pin (3.3 V). */
    gpio_config_t enc = {
        .pin_bit_mask = (1ULL << BV_ENC_CLK_GPIO) | (1ULL << BV_ENC_DT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&enc));

    uint64_t keymask = 0;
    for (size_t i = 0; i < NKEYS; i++) {
        keymask |= 1ULL << s_keys[i].pin;
    }
    gpio_config_t keys = {
        .pin_bit_mask = keymask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&keys));

    s_rstate = R_START;

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {   /* already installed = fine */
        return err;
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(BV_ENC_CLK_GPIO, enc_isr, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BV_ENC_DT_GPIO, enc_isr, NULL));

    /* low priority, core 0: the picture task on core 1 is not disturbed */
    xTaskCreatePinnedToCore(key_task, "input_keys", 2560, NULL, 3, NULL, 0);

    ESP_LOGI(TAG, "Encoder CLK %d, DT %d, SW %d (+ to 3.3 V); %u key(s); "
                  "full-step decoder, long press %d ms",
             BV_ENC_CLK_GPIO, BV_ENC_DT_GPIO, BV_ENC_SW_GPIO, (unsigned)NKEYS,
             BV_INPUT_LONG_MS);
    ESP_LOGI(TAG, "Idle levels: CLK %d, DT %d, SW %d (all should be 1)",
             gpio_get_level(BV_ENC_CLK_GPIO), gpio_get_level(BV_ENC_DT_GPIO),
             gpio_get_level(BV_ENC_SW_GPIO));
    return ESP_OK;
}

/* ---------------- test ---------------- */

static void test_task(void *arg)
{
    QueueHandle_t q = (QueueHandle_t)arg;
    static const char *SRC[] = { "knob", "button 1", "button 2" };
    int pos = 0;
    bv_input_event_t ev;
    while (true) {
        if (xQueueReceive(q, &ev, portMAX_DELAY) == pdTRUE) {
            const char *src = ev.src < 3 ? SRC[ev.src] : "?";
            if (ev.type == BV_IN_TURN) {
                pos += ev.dir;
                ESP_LOGI(TAG, "Input: turn %s (%+d), position %d  [edges %lu]",
                         ev.dir > 0 ? "RIGHT" : "LEFT", ev.dir, pos, (unsigned long)s_edges);
            } else if (ev.type == BV_IN_PRESS) {
                ESP_LOGI(TAG, "Input: %s PRESS", src);
            } else if (ev.type == BV_IN_LONG) {
                ESP_LOGI(TAG, "Input: %s LONG PRESS (2 s)", src);
            }
        }
    }
}

esp_err_t bv_input_test_start(void)
{
    QueueHandle_t q = xQueueCreate(16, sizeof(bv_input_event_t));
    if (!q) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = bv_input_start(q);
    if (err != ESP_OK) {
        return err;
    }
    xTaskCreatePinnedToCore(test_task, "input_test", 2560, q, 3, NULL, 0);
    ESP_LOGI(TAG, "Input TEST mode: turn the knob and press it - every event is logged");
    return ESP_OK;
}
