#include "lowfat_config_stm32.h"

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
    256     /* idx 9: measured maximum request was 200 bytes */
};

size_t lowfat_heap_select(size_t size)
{
    if (size == 0)
        return 0;

    for (size_t i = 1; i <= LOWFAT_NUM_REGIONS; i++)
    {
        if (size <= lowfat_sizes[i])
            return i;
    }

    return 0;
}
