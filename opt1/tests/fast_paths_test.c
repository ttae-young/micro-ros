#include "bench_frame_copy.h"
#include "lowfat_config_stm32.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t reference_class(size_t n)
{
    if (!n) return 0;
    for (size_t i=1; i<=LOWFAT_NUM_REGIONS; ++i)
        if (n<=lowfat_sizes[i]) return i;
    return 0;
}
int main(void)
{
    for (size_t n=0; n<100000; ++n)
        assert(lowfat_heap_select(n)==reference_class(n));
    assert(lowfat_heap_select(SIZE_MAX)==0);
    assert(lowfat_heap_select(SIZE_MAX-15)==0);
    for (uint32_t words=0; words<=26; ++words) {
        uint32_t src[30], dst[30], expected[30];
        for (uint32_t seed=0; seed<100; ++seed) {
            for (unsigned i=0; i<30; ++i) {
                src[i]=seed*0x1020304U+i;
                dst[i]=expected[i]=0xdeadbeef;
            }
            memcpy(expected+1,src+1,words*sizeof(uint32_t));
            bench_copy_exception_frame(dst+1,src+1,words);
            assert(memcmp(dst,expected,sizeof dst)==0);
            for (unsigned i=0; i<30; ++i)
                assert(src[i]==seed*0x1020304U+i);
        }
    }
    puts("PASS size-class equivalence, overflow rejection, frame copy (8/26 words + canaries)");
}
