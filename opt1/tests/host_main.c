#include "lowfat_malloc.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
extern int lowfat_run_tests(void);
void test_log(const char *message) { puts(message); }
static void *worker(void *arg)
{
  uintptr_t id = (uintptr_t)arg;
  for (size_t i = 0; i < 10000; ++i) {
    size_t size = 1 + (i + id) % 128;
    unsigned char *p = lowfat_malloc(size);
    assert(p);
    memset(p, (int)id, size);
    for (size_t j = 0; j < size; ++j) assert(p[j] == (unsigned char)id);
    assert(lowfat_free_checked(p));
  }
  return NULL;
}
int main(void)
{
  int rc = lowfat_run_tests();
  if (rc) { printf("FAILED at test line %d\n", rc); return 1; }
  pthread_t workers[4];
  for (uintptr_t i = 0; i < 4; ++i) assert(pthread_create(&workers[i], NULL, worker, (void *)(i + 1)) == 0);
  for (size_t i = 0; i < 4; ++i) assert(pthread_join(workers[i], NULL) == 0);
  assert(lowfat_live_count() == 0);
  puts("HOST PASS: functional tests + 4 threads / 40000 allocations");
  return 0;
}
