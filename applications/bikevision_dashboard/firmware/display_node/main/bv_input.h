/*
 * bv_input.h - BikeVision display controls (rotary encoder + later 2 buttons).
 *
 * Pins (claude/display-wiring.md):
 *   encoder KY-040: CLK GPIO34, DT GPIO35 (module 10k pull-ups), SW GPIO27
 *                   (internal pull-up), + = 3.3 V
 *   buttons 12x12 (later, BV_INPUT_BUTTONS 1): BTN1 GPIO25, BTN2 GPIO26 to GND
 *
 * Events go into a FreeRTOS queue the caller owns (the display task reads it),
 * so only the display task ever draws.
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BV_IN_TURN  = 1,   /* knob turned one detent: dir +1 = right, -1 = left */
    BV_IN_PRESS = 2,   /* short press (released before the long-press time) */
    BV_IN_LONG  = 3,   /* held for BV_INPUT_LONG_MS (fires once while held) */
} bv_input_type_t;

typedef enum {
    BV_SRC_KNOB = 0,   /* encoder push switch (and turning) */
    BV_SRC_BTN1 = 1,   /* button 1, later: BACK */
    BV_SRC_BTN2 = 2,   /* button 2, later: camera / photo / record */
} bv_input_src_t;

typedef struct {
    uint8_t type;      /* bv_input_type_t */
    uint8_t src;       /* bv_input_src_t */
    int8_t  dir;       /* BV_IN_TURN only */
} bv_input_event_t;

/* Start the encoder (and buttons if enabled). Events are posted to `q`
 * (item size sizeof(bv_input_event_t)); a full queue drops the event. */
esp_err_t bv_input_start(QueueHandle_t q);

/* Test helper: starts the input with its own queue and a task that only
 * logs every event ("Input: ..."). For checking the wiring. */
esp_err_t bv_input_test_start(void);

#ifdef __cplusplus
}
#endif
