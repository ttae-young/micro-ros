#include "lowfat_malloc.h"
#include <string.h>
#include <stdio.h>
extern void test_log(const char *message);
#define REQUIRE(x) do { if (!(x)) { test_log("FAIL: " #x); return __LINE__; } } while (0)
int lowfat_run_tests(void)
{
  REQUIRE(lowfat_malloc_init());
  REQUIRE(lowfat_malloc(0) == NULL);
  REQUIRE(lowfat_malloc(257) == NULL);
  REQUIRE(lowfat_malloc_index(0, 16) == NULL);
  REQUIRE(lowfat_malloc_index(2, 16) == NULL);
  void *indexed = lowfat_malloc_index(1, 16);
  REQUIRE(indexed && lowfat_free_checked(indexed));
  REQUIRE(lowfat_fallback_malloc(16) == NULL);
  REQUIRE(lowfat_fallback_realloc(NULL, 16) == NULL);
  lowfat_fallback_free(NULL);
  REQUIRE(lowfat_calloc(SIZE_MAX, 2) == NULL);
  REQUIRE(lowfat_calloc(0, 16) == NULL);
  unsigned char *a = lowfat_malloc(16), *b = lowfat_malloc(16), *c = lowfat_malloc(32);
  REQUIRE(a && b && c && a != b && a != c);
  uintptr_t old_a = (uintptr_t)a;
  memset(b, 0x5a, 16);
  lowfat_free(a);
  void *d = lowfat_malloc(16);
  REQUIRE((uintptr_t)d == old_a);
  for (size_t i = 0; i < 16; ++i) REQUIRE(b[i] == 0x5a);
  char line[160];
  snprintf(line, sizeof(line), "reuse: a=%p b=%p c=%p d=%p (a == d)", (void *)old_a, (void *)b, (void *)c, d);
  test_log(line);
  lowfat_free(d); lowfat_free(b); lowfat_free(c);
  for (size_t n = 1; n <= 256; ++n) {
    unsigned char *p = lowfat_malloc(n);
    REQUIRE(p && (uintptr_t)p % 16 == 0);
    REQUIRE(lowfat_size(p) >= n);
    REQUIRE(lowfat_base(p + n - 1) == p);
    REQUIRE(lowfat_buffer_size(p + n - 1) == lowfat_size(p) - n + 1);
    memset(p, (int)n, n);
    REQUIRE(lowfat_free_checked(p));
  }
  test_log("PASS: all sizes 1..256, alignment and interior-pointer geometry");
  for (size_t r = 1; r <= LOWFAT_NUM_REGIONS; ++r) {
    void *slots[LOWFAT_REGION_SIZE / LOWFAT_MIN_ALLOC_SIZE];
    size_t count = LOWFAT_REGION_SIZE / lowfat_sizes[r];
    for (size_t i = 0; i < count; ++i) {
      slots[i] = lowfat_malloc(lowfat_sizes[r]);
      REQUIRE(slots[i]);
      for (size_t j = 0; j < i; ++j) REQUIRE(slots[i] != slots[j]);
      memset(slots[i], (int)(i & 255), lowfat_sizes[r]);
    }
    REQUIRE(lowfat_malloc(lowfat_sizes[r]) == NULL);
    REQUIRE(lowfat_calloc(1, lowfat_sizes[r]) == NULL);
    size_t usable = count * lowfat_sizes[r];
    if (usable < LOWFAT_REGION_SIZE) REQUIRE(lowfat_index(LOWFAT_HEAP[r - 1] + usable) == 0);
    for (size_t i = 0; i < count; ++i) {
      for (size_t j = 0; j < lowfat_sizes[r]; ++j)
        REQUIRE(((unsigned char *)slots[i])[j] == (unsigned char)(i & 255));
      REQUIRE(lowfat_free_checked(slots[i]));
    }
  }
  test_log("PASS: every region exhausted, data intact, tail excluded, recovered");
  unsigned char *p = lowfat_calloc(4, 8);
  REQUIRE(p);
  for (size_t i = 0; i < 32; ++i) REQUIRE(p[i] == 0);
  memset(p, 0xa5, 32);
  unsigned char *q = lowfat_realloc(p, 80);
  REQUIRE(q);
  for (size_t i = 0; i < 32; ++i) REQUIRE(q[i] == 0xa5);
  REQUIRE(lowfat_realloc(q, 257) == NULL);
  for (size_t i = 0; i < 32; ++i) REQUIRE(q[i] == 0xa5);
  p = lowfat_realloc(q, 16); REQUIRE(p);
  for (size_t i = 0; i < 16; ++i) REQUIRE(p[i] == 0xa5);
  REQUIRE(lowfat_realloc(p, 0) == NULL);
  test_log("PASS: calloc and realloc preserve data; failed realloc keeps original");
  p = lowfat_malloc(16); REQUIRE(p);
  REQUIRE(lowfat_malloc_init()); /* Idempotent, does not destroy live allocations. */
  REQUIRE(!lowfat_free_checked(p + 1));
  int foreign = 0;
  REQUIRE(!lowfat_free_checked(&foreign));
  REQUIRE(lowfat_free_checked(p));
  REQUIRE(!lowfat_free_checked(p));
  REQUIRE(lowfat_free_checked(NULL));
  REQUIRE(lowfat_live_count() == 0);
  test_log("PASS: invalid/double free rejected; no live allocations");

  p = lowfat_malloc(17); REQUIRE(p);
  REQUIRE(lowfat_requested_size(p) == 17);
  REQUIRE(lowfat_requested_size(p + 16) == 17);
  REQUIRE(lowfat_check_access(p, 17));
  REQUIRE(lowfat_check_access(p + 16, 1));
  REQUIRE(!lowfat_check_access(p + 16, 2));
  REQUIRE(!lowfat_check_access(p + 17, 1));
  REQUIRE(lowfat_malloc_usable_size(p) == 17);
  REQUIRE(lowfat_free_checked(p));
  test_log("PASS: exact requested-size bounds reject over-read/write");

  char *s = lowfat_strdup("micro-ROS");
  REQUIRE(s && strcmp(s, "micro-ROS") == 0);
  char *sn = lowfat_strndup("LowFat", 3);
  REQUIRE(sn && strcmp(sn, "Low") == 0);
  REQUIRE(lowfat_free_checked(s));
  REQUIRE(lowfat_free_checked(sn));
  void *aligned = NULL;
  REQUIRE(lowfat_posix_memalign(&aligned, 16, 32) == 0);
  REQUIRE(aligned && ((uintptr_t)aligned % 16) == 0);
  REQUIRE(lowfat_free_checked(aligned));
  REQUIRE(lowfat_posix_memalign(&aligned, 64, 32) != 0);
  aligned = lowfat_aligned_alloc(16, 32);
  REQUIRE(aligned && lowfat_free_checked(aligned));
  REQUIRE(lowfat_aligned_alloc(16, 17) == NULL);
  REQUIRE(lowfat_valloc(16) == NULL && lowfat_pvalloc(16) == NULL);
  REQUIRE(lowfat_live_count() == 0);
  test_log("PASS: MCU-compatible extra allocation APIs");
  return 0;
}
