#ifndef BENCH_FAST_WORKER_H
#define BENCH_FAST_WORKER_H
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#if BENCH_OPT3 && BENCH_MPU_ENABLED
/* Private service-task API. Static worker lifetime; never export raw handles. */
bool bench_fast_create(unsigned slot, const TaskParameters_t *parameters) __attribute__((section("privileged_functions")));
bool bench_fast_notify(unsigned slot) __attribute__((section("privileged_functions")));
#endif
#endif
