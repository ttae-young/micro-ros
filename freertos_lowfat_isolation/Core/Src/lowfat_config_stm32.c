#include "lowfat_config_stm32.h"
#include "benchmark_config.h"

const size_t lowfat_sizes[LOWFAT_NUM_REGIONS + 1] =
{
    0,      /* idx 0 = invalid / unsupported */
    16,     /* idx 1 */
    32,     /* idx 2 */
    48,     /* idx 3 */
    64,     /* idx 4 */
    80,     /* idx 5 */
    96,     /* idx 6 */
    112,    /* idx 7 */
    128,    /* idx 8 */
    256,    /* original ninth class */
    512, 1024, 2048, 4096 /* headroom for executor/ROS entity allocations */
};

size_t lowfat_heap_select(size_t size)
{
    if (size == 0)
        return 0;

#if BENCH_FAST_PATHS
    /* Exact selection for the fixed 13 classes below. Reject large sizes
     * before arithmetic, including SIZE_MAX; no fallback allocation. */
    if (size <= 128U) return ((size - 1U) >> 4) + 1U;
    if (size <= 512U) return size <= 256U ? 9U : 10U;
    if (size <= 2048U) return size <= 1024U ? 11U : 12U;
    return size <= 4096U ? 13U : 0U;
#else
    for (size_t i = 1; i <= LOWFAT_NUM_REGIONS; i++)
    {
        if (size <= lowfat_sizes[i])
            return i;
    }

    return 0;
#endif
}
