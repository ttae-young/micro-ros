#ifndef BENCH_ALLOCATOR_H
#define BENCH_ALLOCATOR_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint32_t allocate_calls, calloc_calls, realloc_calls, free_calls;
    uint32_t failures, invalid_frees;
    size_t max_request, last_failed_request;
} bench_allocator_stats_t;
bool bench_allocator_init(void);
bench_allocator_stats_t bench_allocator_stats(void);
void *microros_allocate(size_t, void *);
void microros_deallocate(void *, void *);
void *microros_reallocate(void *, size_t, void *);
void *microros_zero_allocate(size_t, size_t, void *);
#endif
