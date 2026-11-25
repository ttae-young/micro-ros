#ifndef BENCH_PAYLOAD_H
#define BENCH_PAYLOAD_H
#include "benchmark_config.h"
#include <stdint.h>

/* Only data lives in the app region. Sequence descriptors stay in trusted RAM. */
typedef struct {
    uint32_t payload[96 / sizeof(uint32_t)];
    uint32_t result, processed, last_value, reserved;
    uint32_t probe_target, probe_result, command;
} app_object_t;

_Static_assert(sizeof(app_object_t) <= BENCH_OBJECT_BYTES, "object exceeds its LowFat slot");
#endif
