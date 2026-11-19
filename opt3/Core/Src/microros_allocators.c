/* All callbacks run in the privileged micro-ROS task in this benchmark.
 * No silent fallback to another heap in LowFat modes. */
#include "bench_allocator.h"
#include "benchmark_config.h"
#include "lowfat_malloc.h"
#include <string.h>
static bench_allocator_stats_t stats;
#if !BENCH_LOWFAT_ENABLED
extern void baseline_allocator_init(void);
extern void *pvPortMallocMicroROS(size_t);
extern void vPortFreeMicroROS(void *);
extern void *pvPortReallocMicroROS(void *, size_t);
#endif
bool bench_allocator_init(void)
{
#if BENCH_LOWFAT_ENABLED
    return lowfat_malloc_init();
#else
    baseline_allocator_init();
    return true;
#endif
}
bench_allocator_stats_t bench_allocator_stats(void) { return stats; }
static void *record(void *p, size_t n)
{
    if (n > stats.max_request) stats.max_request = n;
    if (!p && n) { stats.failures++; stats.last_failed_request = n; }
    return p;
}
void *microros_allocate(size_t n, void *state)
{
    (void)state; stats.allocate_calls++;
#if BENCH_LOWFAT_ENABLED
    return record(lowfat_malloc(n), n);
#else
    return record(pvPortMallocMicroROS(n), n);
#endif
}
void microros_deallocate(void *p, void *state)
{
    (void)state; stats.free_calls++;
#if BENCH_LOWFAT_ENABLED
    if (!lowfat_free_checked(p)) stats.invalid_frees++;
#else
    vPortFreeMicroROS(p);
#endif
}
void *microros_reallocate(void *p, size_t n, void *state)
{
    (void)state; stats.realloc_calls++;
    if (!n) { microros_deallocate(p, NULL); return NULL; }
#if BENCH_LOWFAT_ENABLED
    return record(lowfat_realloc(p, n), n);
#else
    return record(pvPortReallocMicroROS(p, n), n);
#endif
}
void *microros_zero_allocate(size_t count, size_t size, void *state)
{
    (void)state; stats.calloc_calls++;
    if (size && count > SIZE_MAX / size) return record(NULL, SIZE_MAX);
    size_t n = count * size;
#if BENCH_LOWFAT_ENABLED
    return record(lowfat_calloc(count, size), n);
#else
    void *p = pvPortMallocMicroROS(n);
    if (p) memset(p, 0, n);
    return record(p, n);
#endif
}
