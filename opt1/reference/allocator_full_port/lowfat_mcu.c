/* MCU adaptation of the supplied size-class / bump-pointer / LIFO free-list
 * design. Linux mmap, constructors, fallback, aliases and guard pages omitted.
 * Configuration is copied unchanged from the supplied STM32 configuration. */
#include "lowfat_malloc.h"
#include <string.h>
#ifdef LOWFAT_HOST_TEST
#include <pthread.h>
#include <assert.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static void lock(void) { int rc = pthread_mutex_lock(&mutex); assert(rc == 0); }
static void unlock(void) { int rc = pthread_mutex_unlock(&mutex); assert(rc == 0); }
#else
#include "FreeRTOS.h"
#include "semphr.h"

static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex;
static void lock(void) { BaseType_t ok = xSemaphoreTake(mutex, portMAX_DELAY); configASSERT(ok == pdTRUE); }
static void unlock(void) { BaseType_t ok = xSemaphoreGive(mutex); configASSERT(ok == pdTRUE); }
#endif

#define MAX_SLOTS (LOWFAT_REGION_SIZE / LOWFAT_MIN_ALLOC_SIZE)

_Alignas(16) uint8_t LOWFAT_HEAP[LOWFAT_NUM_REGIONS][LOWFAT_REGION_SIZE];
static struct { size_t used; void *freelist; } regions[LOWFAT_NUM_REGIONS];
/* 0 = unallocated, otherwise original request length. */
static size_t requested[LOWFAT_NUM_REGIONS][MAX_SLOTS];
static size_t live;
static bool initialized;
_Static_assert(LOWFAT_REGION_SIZE % 16 == 0, "region alignment");
_Static_assert(LOWFAT_MIN_ALLOC_SIZE >= sizeof(void *), "free-list pointer must fit");

bool lowfat_malloc_init(void)
{
  if (initialized) return true; /* Never reset a live allocator. */
  for (size_t i = 1; i <= LOWFAT_NUM_REGIONS; ++i)
    if (lowfat_sizes[i] < LOWFAT_MIN_ALLOC_SIZE || lowfat_sizes[i] % 16 ||
        lowfat_sizes[i] > LOWFAT_REGION_SIZE) return false;
#ifndef LOWFAT_HOST_TEST
  mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
  if (!mutex) return false;
#endif
  initialized = true;
  return true;
}

/* [주소 할당 계산] */

size_t lowfat_index(const void *ptr)
{
  uintptr_t p = (uintptr_t)ptr, start = (uintptr_t)&LOWFAT_HEAP[0][0];
  if (p < start || p - start >= sizeof(LOWFAT_HEAP)) return 0;
  size_t index = (p - start) / LOWFAT_REGION_SIZE;
  size_t offset = (p - start) % LOWFAT_REGION_SIZE;
  size_t size = lowfat_sizes[index + 1];
  /* E.g. 48-byte objects leave a 32-byte unusable tail in a 2048-byte region. */
  if (offset >= (LOWFAT_REGION_SIZE / size) * size) return 0;
  return index + 1;
}
size_t lowfat_size(const void *ptr)
{
  return lowfat_sizes[lowfat_index(ptr)];
}
void *lowfat_base(const void *ptr)
{
  size_t index = lowfat_index(ptr);
  if (!index) return NULL;
  uintptr_t start = (uintptr_t)LOWFAT_HEAP[index - 1];
  size_t size = lowfat_sizes[index];
  return (void *)(start + (((uintptr_t)ptr - start) / size) * size);
}
size_t lowfat_buffer_size(const void *ptr)
{
  void *base = lowfat_base(ptr);
  return base ? lowfat_size(ptr) - ((uintptr_t)ptr - (uintptr_t)base) : 0;
}

static bool locate_live(void *ptr, size_t *region, size_t *slot)
{
  size_t index = lowfat_index(ptr);
  if (!index || lowfat_base(ptr) != ptr) return false;
  *region = index - 1;
  *slot = ((uintptr_t)ptr - (uintptr_t)LOWFAT_HEAP[*region]) / lowfat_sizes[index];
  return requested[*region][*slot] != 0;
}

static bool locate_containing_live(const void *ptr, size_t *region,
                                   size_t *slot, void **base)
{
  size_t index = lowfat_index(ptr);
  if (!index) return false;
  void *object = lowfat_base(ptr);
  if (!object) return false;
  *region = index - 1;
  *slot = ((uintptr_t)object -
           (uintptr_t)LOWFAT_HEAP[*region]) / lowfat_sizes[index];
  if (requested[*region][*slot] == 0) return false;
  if (base) *base = object;
  return true;
}

static void *allocate_locked(size_t size)
{
  size_t index = lowfat_heap_select(size);
  if (!index) return NULL;
  size_t r = index - 1, block = lowfat_sizes[index];
  void *ptr = regions[r].freelist;
  if (ptr) {
    memcpy(&regions[r].freelist, ptr, sizeof(void *));
  } else {
    if (block > LOWFAT_REGION_SIZE - regions[r].used) return NULL;
    ptr = LOWFAT_HEAP[r] + regions[r].used;
    regions[r].used += block;
  }
  size_t slot = ((uintptr_t)ptr - (uintptr_t)LOWFAT_HEAP[r]) / block;
  requested[r][slot] = size;
  ++live;
  return ptr;
}

void *lowfat_malloc_index(size_t index, size_t size)
{
  if (!initialized || !index || index > LOWFAT_NUM_REGIONS ||
      size == 0 || size > lowfat_sizes[index] ||
      lowfat_heap_select(size) != index) return NULL;
  lock();
  void *ptr = allocate_locked(size);
  unlock();
  return ptr;
}

static bool free_locked(void *ptr)
{
  if (!ptr) return true;
  size_t r, slot;
  if (!locate_live(ptr, &r, &slot)) return false;
  requested[r][slot] = 0;
  memcpy(ptr, &regions[r].freelist, sizeof(void *));
  regions[r].freelist = ptr;
  --live;
  return true;
}

void *lowfat_malloc(size_t size)
{
  if (!initialized) return NULL;
  lock(); void *ptr = allocate_locked(size); unlock();
  return ptr;
}

bool lowfat_free_checked(void *ptr)
{
  if (!ptr) return true;
  if (!initialized) return false;
  lock(); bool ok = free_locked(ptr); unlock();
  return ok;
}
void lowfat_free(void *ptr) { (void)lowfat_free_checked(ptr); }
void *lowfat_calloc(size_t count, size_t size)
{
  if (size && count > SIZE_MAX / size) return NULL;
  size_t bytes = count * size;
  void *ptr = lowfat_malloc(bytes);
  if (ptr) memset(ptr, 0, bytes);
  return ptr;
}
void *lowfat_realloc(void *ptr, size_t size)
{
  if (!ptr) return lowfat_malloc(size);
  if (!initialized) return NULL;
  lock();
  size_t r, slot;
  if (!locate_live(ptr, &r, &slot)) { unlock(); return NULL; }
  if (!size) { (void)free_locked(ptr); unlock(); return NULL; }
  if (lowfat_heap_select(size) == r + 1) {
    requested[r][slot] = size; unlock(); return ptr;
  }
  void *next = allocate_locked(size);
  if (next) {
    size_t old_size = requested[r][slot];
    memcpy(next, ptr, size < old_size ? size : old_size);
    (void)free_locked(ptr);
  }
  unlock(); return next;
}
size_t lowfat_live_count(void)
{
  if (!initialized) return 0;
  lock(); size_t count = live; unlock(); return count;
}


size_t lowfat_requested_size(const void *ptr)
{
  if (!initialized || !ptr) return 0;
  lock();
  size_t r, slot;
  void *base;
  size_t result = locate_containing_live(ptr, &r, &slot, &base) ?
                  requested[r][slot] : 0;
  unlock();
  return result;
}

bool lowfat_check_access(const void *ptr, size_t access_size)
{
  if (!initialized || !ptr) return false;
  lock();
  size_t r, slot;
  void *base;
  bool ok = locate_containing_live(ptr, &r, &slot, &base);
  if (ok) {
    size_t offset = (size_t)((uintptr_t)ptr - (uintptr_t)base);
    size_t requested_size = requested[r][slot];
    ok = offset <= requested_size && access_size <= requested_size - offset;
  }
  unlock();
  return ok;
}

bool lowfat_is_ptr(const void *ptr) { return lowfat_index(ptr) != 0; }
bool lowfat_is_heap_ptr(const void *ptr) { return lowfat_is_ptr(ptr); }
bool lowfat_is_stack_ptr(const void *ptr) { (void)ptr; return false; }
