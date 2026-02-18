#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t count;
    uint32_t pulse_ms;   // 30..300
} pulse_req_t;

bool pulse_engine_init(int gpio_out);
bool pulse_engine_request(pulse_req_t req);
