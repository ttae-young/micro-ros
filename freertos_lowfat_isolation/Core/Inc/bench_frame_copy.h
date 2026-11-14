#ifndef BENCH_FRAME_COPY_H
#define BENCH_FRAME_COPY_H
#include <stdint.h>
#include "benchmark_config.h"

/* Separate, non-overlapping word-aligned task/system-call stacks.
 * Keep all entry/exit validation, padding, PSPLIM and privilege logic in port.c.
 * Standard Cortex-M exception frame: 8 words. Extended FPU frame: 26 words.
 * No floating-point instructions or changes to lazy FPU stacking are needed. */
#if BENCH_OPT3
/* Both port call sites use distinct task and syscall stack allocations.
 * Expose that existing non-aliasing invariant to the compiler. */
#define BENCH_FRAME_RESTRICT __restrict
#else
#define BENCH_FRAME_RESTRICT
#endif
static inline __attribute__((always_inline)) void bench_copy_exception_frame(
    uint32_t * BENCH_FRAME_RESTRICT dst, const uint32_t * BENCH_FRAME_RESTRICT src, uint32_t words)
{
#if BENCH_FAST_PATHS
    if (words == 8U) {
        dst[0] = src[0]; dst[1] = src[1];
        dst[2] = src[2]; dst[3] = src[3];
        dst[4] = src[4]; dst[5] = src[5];
        dst[6] = src[6]; dst[7] = src[7];
        return;
    }
#endif
    /* Preserve the original general/extended-frame path. */
    for (uint32_t i = 0; i < words; ++i) dst[i] = src[i];
}
#endif
