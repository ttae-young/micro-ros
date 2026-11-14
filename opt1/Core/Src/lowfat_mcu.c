/* MCU adaptation of the supplied size-class / bump-pointer / LIFO free-list
 * design. Linux mmap, constructors, fallback, aliases and guard pages omitted.
 * Recovered from allocator_full_port. Pool geometry extended for this workload;
 * allocation/free-list/base calculation/requested-size bounds logic retained. */
#include "benchmark_config.h"
#if BENCH_OPT3 && !defined(LOWFAT_HOST_TEST)
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "FreeRTOS.h"
#include "semphr.h"
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#endif
#include "lowfat_malloc.h"
#include "benchmark_config.h"
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
static void lock(void) PRIVILEGED_FUNCTION;
static void unlock(void) PRIVILEGED_FUNCTION;
static void lock(void) { BaseType_t ok = xSemaphoreTake(mutex, portMAX_DELAY); configASSERT(ok == pdTRUE); }
static void unlock(void) { BaseType_t ok = xSemaphoreGive(mutex); configASSERT(ok == pdTRUE); }
#endif

#define MAX_SLOTS (LOWFAT_REGION_SIZE / LOWFAT_MIN_ALLOC_SIZE)

#ifdef LOWFAT_HOST_TEST
_Alignas(4096) uint8_t LOWFAT_HEAP[LOWFAT_NUM_REGIONS][LOWFAT_REGION_SIZE];
#else
uint8_t LOWFAT_HEAP[LOWFAT_NUM_REGIONS][LOWFAT_REGION_SIZE]
    __attribute__((section(".lowfat_heap"), aligned(4096)));
#endif
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
  memset(LOWFAT_HEAP, 0, sizeof(LOWFAT_HEAP)); /* Explicit init of NOLOAD SRAM2. */
  mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
  if (!mutex) return false;
#endif
  initialized = true;
  return true;
}

/* Compute geometry once per operation. Keep exact requested-size and live-slot
 * checks, including the unused tail of non-power-of-two size classes. */
typedef struct {
  size_t region, slot, block, offset, object_offset;
} geometry_t;

#if BENCH_OPT3
#define LF_INLINE static inline __attribute__((always_inline))
#else
#define LF_INLINE static
#endif
/* Power-of-two classes avoid runtime division; non-power-of-two classes
 * retain exact division and unused-tail validation. */
static size_t slot_for(size_t offset, size_t region, size_t block)
{
#if BENCH_OPT3
  static const uint8_t shifts[LOWFAT_NUM_REGIONS]={4,5,0,6,0,0,0,7,8,9,10,11,12};
  unsigned shift=shifts[region];
  if (shift && block==((size_t)1U<<shift)) return offset>>shift;
#else
  (void)region;
#endif
  return offset/block;
}
LF_INLINE bool decode_pointer(const void *ptr, geometry_t *g)
{
  uintptr_t p = (uintptr_t)ptr, start = (uintptr_t)&LOWFAT_HEAP[0][0];
  if (p < start || p - start >= sizeof(LOWFAT_HEAP)) return false;
  size_t delta = (size_t)(p - start);
  g->region = delta / LOWFAT_REGION_SIZE;
  g->offset = delta % LOWFAT_REGION_SIZE;
  g->block = lowfat_sizes[g->region + 1];
  g->slot = slot_for(g->offset, g->region, g->block);
  g->object_offset = g->slot * g->block;
  /* Equivalent to slot < REGION_SIZE / block, without a second division. */
  return g->object_offset <= LOWFAT_REGION_SIZE - g->block;
}

size_t lowfat_index(const void *ptr)
{
  geometry_t g;
  return decode_pointer(ptr, &g) ? g.region + 1 : 0;
}
size_t lowfat_size(const void *ptr)
{
  return lowfat_sizes[lowfat_index(ptr)];
}
void *lowfat_base(const void *ptr)
{
  geometry_t g;
  return decode_pointer(ptr, &g) ? LOWFAT_HEAP[g.region] + g.object_offset : NULL;
}
size_t lowfat_buffer_size(const void *ptr)
{
  geometry_t g;
  return decode_pointer(ptr, &g) ? g.block - (g.offset - g.object_offset) : 0;
}

LF_INLINE bool locate_live(void *ptr, size_t *region, size_t *slot)
{
  geometry_t g;
  if (!decode_pointer(ptr, &g) || g.offset != g.object_offset) return false;
  *region = g.region;
  *slot = g.slot;
  return requested[*region][*slot] != 0;
}

LF_INLINE bool locate_containing_live(const void *ptr, size_t *region,
                                   size_t *slot, void **base)
{
  geometry_t g;
  if (!decode_pointer(ptr, &g)) return false;
  *region = g.region;
  *slot = g.slot;
  if (requested[*region][*slot] == 0) return false;
  if (base) *base = LOWFAT_HEAP[g.region] + g.object_offset;
  return true;
}

/* Caller has validated the size-class index and holds the mutex. */
static void *allocate_selected_locked(size_t index, size_t size)
{
  size_t r = index - 1, block = lowfat_sizes[index];
  void *ptr = regions[r].freelist;
  if (ptr) {
    memcpy(&regions[r].freelist, ptr, sizeof(void *));
  } else {
    if (block > LOWFAT_REGION_SIZE - regions[r].used) return NULL;
    ptr = LOWFAT_HEAP[r] + regions[r].used;
    regions[r].used += block;
  }
  size_t slot = slot_for((uintptr_t)ptr - (uintptr_t)LOWFAT_HEAP[r], r, block);
  requested[r][slot] = size;
  ++live;
  return ptr;
}

static void *allocate_locked(size_t size)
{
  size_t index = lowfat_heap_select(size);
  return index ? allocate_selected_locked(index, size) : NULL;
}

void *lowfat_malloc_index(size_t index, size_t size)
{
  if (!initialized || !index || index > LOWFAT_NUM_REGIONS ||
      size == 0 || size > lowfat_sizes[index] ||
      lowfat_heap_select(size) != index) return NULL;
  lock();
#if BENCH_FAST_PATHS
  void *ptr = allocate_selected_locked(index, size);
#else
  void *ptr = allocate_locked(size);
#endif
  unlock();
  return ptr;
}

static void release_slot_locked(void *ptr, size_t r, size_t slot)
{
  requested[r][slot] = 0;
  memcpy(ptr, &regions[r].freelist, sizeof(void *));
  regions[r].freelist = ptr;
  --live;
}
static bool free_locked(void *ptr)
{
  if (!ptr) return true;
  size_t r, slot;
  if (!locate_live(ptr, &r, &slot)) return false;
  release_slot_locked(ptr,r,slot);
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
  if (!size) {
#if BENCH_OPT3
    release_slot_locked(ptr,r,slot);
#else
    (void)free_locked(ptr);
#endif
    unlock(); return NULL;
  }
  if (lowfat_heap_select(size) == r + 1) {
    requested[r][slot] = size; unlock(); return ptr;
  }
  void *next = allocate_locked(size);
  if (next) {
    size_t old_size = requested[r][slot];
    memcpy(next, ptr, size < old_size ? size : old_size);
#if BENCH_OPT3
    release_slot_locked(ptr,r,slot);
#else
    (void)free_locked(ptr);
#endif
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
