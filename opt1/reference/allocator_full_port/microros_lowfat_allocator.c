#include "microros_lowfat_allocator.h"
#include "lowfat_malloc.h"
#include "rcutils/allocator.h"

static void *uros_allocate(size_t size, void *state)
{
  (void)state;
  return lowfat_malloc(size);
}

static void uros_deallocate(void *pointer, void *state)
{
  (void)state;
  lowfat_free(pointer);
}

static void *uros_reallocate(void *pointer, size_t size, void *state)
{
  (void)state;
  return lowfat_realloc(pointer, size);
}

static void *uros_zero_allocate(size_t count, size_t size, void *state)
{
  (void)state;
  return lowfat_calloc(count, size);
}

rcutils_allocator_t lowfat_rcutils_allocator(void)
{
  rcutils_allocator_t allocator = rcutils_get_zero_initialized_allocator();
  allocator.allocate = uros_allocate;
  allocator.deallocate = uros_deallocate;
  allocator.reallocate = uros_reallocate;
  allocator.zero_allocate = uros_zero_allocate;
  allocator.state = NULL;
  return allocator;
}

bool lowfat_install_microros_allocator(void)
{
  if (!lowfat_malloc_init()) return false;
  rcutils_allocator_t allocator = lowfat_rcutils_allocator();
  return rcutils_set_default_allocator(&allocator) == RCUTILS_RET_OK;
}
