/*
 * link_test - packet formats for the ESP-NOW link test (task 1).
 * Shared by sender_node and receiver_node.
 */
#pragma once

#include <stdint.h>
#include "espnow_link.h"

#define LINK_TEST_MAX_PACKETS  2000

enum {
    LINK_TEST_TYPE_DATA   = ESPNOW_LINK_TYPE_APP_FIRST + 0, /* sender -> receiver */
    LINK_TEST_TYPE_END    = ESPNOW_LINK_TYPE_APP_FIRST + 1, /* sender -> receiver */
    LINK_TEST_TYPE_RESULT = ESPNOW_LINK_TYPE_APP_FIRST + 2, /* receiver -> sender */
};

typedef struct __attribute__((packed)) {
    uint32_t run_id;
    uint32_t seq;     /* 0 .. total-1 */
    uint32_t total;
    /* followed by a fill pattern, see link_test_fill() */
} link_test_data_t;

typedef struct __attribute__((packed)) {
    uint32_t run_id;
    uint32_t total;
    uint32_t sender_ok;   /* packets the sender saw ACKed */
} link_test_end_t;

typedef struct __attribute__((packed)) {
    uint32_t run_id;
    uint32_t total;
    uint32_t received;    /* unique packets received */
    uint32_t duplicates;
    uint32_t out_of_order;
    uint32_t corrupted;
    uint32_t duration_ms; /* first to last packet */
    int8_t   rssi_min;
    int8_t   rssi_max;
} link_test_result_t;

/* Payload size used for every data packet: the full ESP-NOW payload. */
#define LINK_TEST_DATA_LEN  ESPNOW_LINK_MAX_PAYLOAD

/* Deterministic fill so the receiver can detect corrupted packets. */
static inline uint8_t link_test_fill(uint32_t seq, uint32_t i)
{
    return (uint8_t)(seq * 7u + i);
}
