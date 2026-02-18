#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t count;
    uint32_t pulse_ms;   // 30..300
} pulse_req_t;

bool pulse_engine_init(int gpio_out);



typedef enum {
    PULSE_OK = 0,
    PULSE_ERR_NOT_INIT = -1,
    PULSE_ERR_BUSY = -2,
    PULSE_ERR_QUEUE_FULL = -3,
} pulse_rc_t;

typedef struct {
    uint32_t pulses_done;
    uint32_t elapsed_ms;
} pulse_done_t;

typedef void (*pulse_done_cb_t)(pulse_done_t done);

pulse_rc_t pulse_engine_request(pulse_req_t req);

void pulse_engine_set_done_cb(pulse_done_cb_t cb);
bool pulse_engine_is_busy(void);
