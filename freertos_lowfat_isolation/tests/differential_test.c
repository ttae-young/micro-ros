/* Compare observable allocator semantics against the actual pre-change source. */
#include "lowfat_malloc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern uint8_t ref_LOW_FAT_HEAP[LOWFAT_NUM_REGIONS][LOWFAT_REGION_SIZE];
bool ref_lowfat_malloc_init(void);
void *ref_lowfat_malloc(size_t);
void *ref_lowfat_realloc(void *, size_t);
bool ref_lowfat_free_checked(void *);
size_t ref_lowfat_index(const void *);
size_t ref_lowfat_size(const void *);
void *ref_lowfat_base(const void *);
size_t ref_lowfat_buffer_size(const void *);
size_t ref_lowfat_requested_size(const void *);
bool ref_lowfat_check_access(const void *, size_t);
size_t ref_lowfat_live_count(void);
static uintptr_t relative(const void *p, const void *heap)
{ return p ? (uintptr_t)p - (uintptr_t)heap : UINTPTR_MAX; }
static void compare(const void *p, const void *q)
{
    assert(lowfat_index(p) == ref_lowfat_index(q));
    assert(lowfat_size(p) == ref_lowfat_size(q));
    assert(lowfat_buffer_size(p) == ref_lowfat_buffer_size(q));
    assert(relative(lowfat_base(p), LOWFAT_HEAP) == relative(ref_lowfat_base(q), ref_LOW_FAT_HEAP));
    assert(lowfat_requested_size(p) == ref_lowfat_requested_size(q));
    const size_t lengths[] = {0,1,2,16,17,31,32,48,80,256,4096,SIZE_MAX};
    for (size_t j = 0; j < sizeof(lengths)/sizeof(lengths[0]); ++j)
        assert(lowfat_check_access(p,lengths[j]) == ref_lowfat_check_access(q,lengths[j]));
}
static void scan(void)
{
    compare(NULL, NULL);
    for (size_t i = 0; i <= sizeof(LOWFAT_HEAP); ++i)
        compare((void *)((uintptr_t)LOWFAT_HEAP+i), (void *)((uintptr_t)ref_LOW_FAT_HEAP+i));
    compare((void *)((uintptr_t)LOWFAT_HEAP-1), (void *)((uintptr_t)ref_LOW_FAT_HEAP-1));
    compare((void *)UINTPTR_MAX, (void *)UINTPTR_MAX);
}
int main(void)
{
    scan(); /* Preserve queries before initialization too. */
    assert(lowfat_malloc_init() && ref_lowfat_malloc_init());
    unsigned char *p[128] = {0}, *q[128] = {0};
    size_t sizes[128] = {0};
    uint32_t rng = 17;
    for (unsigned t = 0; t < 10000; ++t) {
        rng = rng * 1664525U + 1013904223U;
        unsigned k = (rng >> 16) % 128;
        size_t n = (rng >> 3) % 5000;
        if ((rng & 3) == 0) {
            assert(lowfat_free_checked(p[k]) == ref_lowfat_free_checked(q[k]));
            p[k] = q[k] = NULL; sizes[k] = 0;
        } else {
            unsigned char *a = lowfat_realloc(p[k], n);
            unsigned char *b = ref_lowfat_realloc(q[k], n);
            assert(relative(a, LOWFAT_HEAP) == relative(b, ref_LOW_FAT_HEAP));
            if (a) {
                size_t preserved = sizes[k] < n ? sizes[k] : n;
                for (size_t j = 0; j < preserved; ++j) assert(a[j] == (unsigned char)k && b[j] == (unsigned char)k);
                memset(a, (int)k, n); memset(b, (int)k, n);
                p[k] = a; q[k] = b; sizes[k] = n;
            } else if (!n) { p[k] = q[k] = NULL; sizes[k] = 0; }
        }
        if (p[k]) {
            compare(p[k], q[k]);
            compare(p[k] + sizes[k] - 1, q[k] + sizes[k] - 1);
            assert(!lowfat_free_checked(p[k]+1) && !ref_lowfat_free_checked(q[k]+1));
        }
        assert(lowfat_live_count() == ref_lowfat_live_count());
    }
    scan(); /* Check every pool byte, region tails, live and free slots. */
    for (unsigned k = 0; k < 128; ++k) {
        assert(lowfat_free_checked(p[k]) && ref_lowfat_free_checked(q[k]));
    }
    assert(!lowfat_live_count() && !ref_lowfat_live_count());
    scan();
    puts("DIFFERENTIAL PASS: all pool offsets and 10000 allocator operations match original semantics");
    return 0;
}
