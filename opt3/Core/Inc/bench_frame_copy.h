#ifndef BENCH_FRAME_COPY_H
#define BENCH_FRAME_COPY_H
#include <stdint.h>
#include "benchmark_config.h"

/* Separate, non-overlapping word-aligned task/system-call stacks.
 * Keep all entry/exit validation, padding, PSPLIM and privilege logic in port.c.
 * Standard Cortex-M exception frame: 8 words. Extended FPU frame: 26 words.
 * No floating-point instructions or changes to lazy FPU stacking are needed. */
#if BENCH_OPT3 || BENCH_TUNE_STAGE >= 1
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
#if BENCH_TUNE_STAGE >= 1
    if (words == 26U) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
        dst[4] = src[4];
        dst[5] = src[5];
        dst[6] = src[6];
        dst[7] = src[7];
        dst[8] = src[8];
        dst[9] = src[9];
        dst[10] = src[10];
        dst[11] = src[11];
        dst[12] = src[12];
        dst[13] = src[13];
        dst[14] = src[14];
        dst[15] = src[15];
        dst[16] = src[16];
        dst[17] = src[17];
        dst[18] = src[18];
        dst[19] = src[19];
        dst[20] = src[20];
        dst[21] = src[21];
        dst[22] = src[22];
        dst[23] = src[23];
        dst[24] = src[24];
        dst[25] = src[25];
        return;
    }
#endif
    /* Preserve the original general/extended-frame path. */
    for (uint32_t i = 0; i < words; ++i) dst[i] = src[i];
}
/* Same non-overlapping buffers as the original copier. Patch core frame
 * fields once, preserving every FP word and all caller state checks. */
static inline __attribute__((always_inline)) void bench_copy_patch_frame(
    uint32_t * __restrict dst, const uint32_t * __restrict src,
    uint32_t words, uint32_t lr, uint32_t pc, uint32_t psr)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    dst[4] = src[4];
    dst[5] = lr;
    dst[6] = pc;
    dst[7] = psr;
    if (words == 26U) {
        dst[8] = src[8];
        dst[9] = src[9];
        dst[10] = src[10];
        dst[11] = src[11];
        dst[12] = src[12];
        dst[13] = src[13];
        dst[14] = src[14];
        dst[15] = src[15];
        dst[16] = src[16];
        dst[17] = src[17];
        dst[18] = src[18];
        dst[19] = src[19];
        dst[20] = src[20];
        dst[21] = src[21];
        dst[22] = src[22];
        dst[23] = src[23];
        dst[24] = src[24];
        dst[25] = src[25];
    }
}
#endif
