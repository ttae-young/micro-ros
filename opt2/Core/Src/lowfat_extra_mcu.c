#include "lowfat_malloc.h"
#include <string.h>

#define LOWFAT_EINVAL 22
#define LOWFAT_ENOMEM 12
#define LOWFAT_MCU_ALIGNMENT 16u
#define LOWFAT_MCU_PAGE_SIZE 4096u

static size_t bounded_strlen(const char *str, size_t limit)
{
  size_t n = 0;
  while (n < limit && str[n] != '\0') ++n;
  return n;
}

static size_t tracked_buffer_remaining(const char *str)
{
  size_t requested = lowfat_requested_size(str);
  void *base = lowfat_base(str);
  if (!requested || !base) return 0;
  size_t offset = (size_t)((uintptr_t)str - (uintptr_t)base);
  return offset <= requested ? requested - offset : 0;
}

size_t lowfat_malloc_usable_size(const void *ptr)
{
  return lowfat_requested_size(ptr);
}

int lowfat_posix_memalign(void **memptr, size_t alignment, size_t size)
{
  if (!memptr || alignment < sizeof(void *) ||
      (alignment & (alignment - 1u)) != 0u) return LOWFAT_EINVAL;
  *memptr = NULL;
  /* Every pool block is 16-byte aligned. Larger alignments need a dedicated
   * size class and are deliberately rejected instead of returning a pointer
   * that lowfat_free_checked() cannot validate safely. */
  if (alignment > LOWFAT_MCU_ALIGNMENT) return LOWFAT_ENOMEM;
  *memptr = lowfat_malloc(size);
  return *memptr ? 0 : LOWFAT_ENOMEM;
}

void *lowfat_memalign(size_t alignment, size_t size)
{
  void *ptr = NULL;
  return lowfat_posix_memalign(&ptr, alignment, size) == 0 ? ptr : NULL;
}

void *lowfat_aligned_alloc(size_t alignment, size_t size)
{
  if (!alignment || size % alignment != 0u) return NULL;
  return lowfat_memalign(alignment, size);
}

void *lowfat_valloc(size_t size)
{
  (void)size;
  return NULL; /* MCU pool does not provide page-aligned virtual memory. */
}

void *lowfat_pvalloc(size_t size)
{
  (void)size;
  (void)LOWFAT_MCU_PAGE_SIZE;
  return NULL;
}

char *lowfat_strdup(const char *str)
{
  if (!str) return NULL;
  size_t limit = lowfat_is_ptr(str) ? tracked_buffer_remaining(str) : SIZE_MAX;
  size_t len = bounded_strlen(str, limit);
  if (limit != SIZE_MAX && len == limit) return NULL;
  char *copy = (char *)lowfat_malloc(len + 1u);
  if (!copy) return NULL;
  memcpy(copy, str, len + 1u);
  return copy;
}

char *lowfat_strndup(const char *str, size_t maxlen)
{
  if (!str) return NULL;
  size_t limit = lowfat_is_ptr(str) ? tracked_buffer_remaining(str) : maxlen;
  if (limit > maxlen) limit = maxlen;
  size_t len = bounded_strlen(str, limit);
  char *copy = (char *)lowfat_malloc(len + 1u);
  if (!copy) return NULL;
  memcpy(copy, str, len);
  copy[len] = '\0';
  return copy;
}
