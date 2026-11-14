#include "bench_allocator.h"
#include "lowfat_malloc.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    assert(bench_allocator_init());
    unsigned char *objects[5];
    for (unsigned i = 0; i < 5; ++i) {
        objects[i] = microros_zero_allocate(1, 32, NULL);
        assert(objects[i] && ((uintptr_t)objects[i] % 32) == 0);
        assert(lowfat_size(objects[i]) == 32 && lowfat_requested_size(objects[i]) == 32);
        for (unsigned j = 0; j < 32; ++j) assert(objects[i][j] == 0);
        for (unsigned j = 0; j < i; ++j) assert(objects[i] != objects[j]);
    }
    unsigned char *p = microros_allocate(17, NULL);
    assert(p); memset(p, 0x42, 17);
    unsigned char *q = microros_reallocate(p, 80, NULL);
    assert(q);
    for (unsigned i = 0; i < 17; ++i) assert(q[i] == 0x42);
    assert(!microros_reallocate(q, LOWFAT_MAX_ALLOC_SIZE + 1, NULL));
    assert(lowfat_check_access(q, 80));
    assert(!microros_zero_allocate(SIZE_MAX, 2, NULL));
    assert(!microros_reallocate(q, 0, NULL));
    assert(!lowfat_check_access(q, 1));
    for (unsigned i = 0; i < 5; ++i) microros_deallocate(objects[i], NULL);
    microros_deallocate(NULL, NULL);
    bench_allocator_stats_t s = bench_allocator_stats();
    assert(s.failures == 2 && s.invalid_frees == 0 && lowfat_live_count() == 0);
    puts("ADAPTER PASS: MPU slot alignment, calloc/realloc/free, overflow, no fallback");
    return 0;
}
