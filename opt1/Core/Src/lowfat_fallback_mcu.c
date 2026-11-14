#include "lowfat_malloc.h"

/* The Linux implementation forwards unsupported requests to glibc through
 * __libc_malloc/__libc_realloc/__libc_free. An MCU has no compatible libc
 * heap ownership contract, and mixing heaps makes free/realloc unsafe.
 * Unsupported requests therefore fail explicitly. */
void *lowfat_fallback_malloc(size_t size)
{
  (void)size;
  return NULL;
}

void lowfat_fallback_free(void *ptr)
{
  (void)ptr;
}

void *lowfat_fallback_realloc(void *ptr, size_t size)
{
  (void)ptr;
  (void)size;
  return NULL;
}
