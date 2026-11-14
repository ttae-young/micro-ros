#ifndef LOWFAT_MCU_MALLOC_H
#define LOWFAT_MCU_MALLOC_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "lowfat_config_stm32.h"
/* Call init once before sharing allocator with other tasks. Task context only.
 * Exact allocation pointers only for free/realloc; no malloc symbol interposition. */
bool lowfat_malloc_init(void);
void *lowfat_malloc(size_t size);
void *lowfat_malloc_index(size_t index, size_t size);
void lowfat_free(void *ptr);
bool lowfat_free_checked(void *ptr);
void *lowfat_calloc(size_t count, size_t size);
void *lowfat_realloc(void *ptr, size_t size);
void *lowfat_fallback_malloc(size_t size);
void lowfat_fallback_free(void *ptr);
void *lowfat_fallback_realloc(void *ptr, size_t size);
int lowfat_posix_memalign(void **memptr, size_t alignment, size_t size);
void *lowfat_memalign(size_t alignment, size_t size);
void *lowfat_aligned_alloc(size_t alignment, size_t size);
void *lowfat_valloc(size_t size);
void *lowfat_pvalloc(size_t size);
char *lowfat_strdup(const char *str);
char *lowfat_strndup(const char *str, size_t maxlen);
size_t lowfat_malloc_usable_size(const void *ptr);
size_t lowfat_index(const void *ptr);
size_t lowfat_size(const void *ptr);
void *lowfat_base(const void *ptr);
size_t lowfat_buffer_size(const void *ptr);
size_t lowfat_requested_size(const void *ptr);
bool lowfat_check_access(const void *ptr, size_t access_size);
bool lowfat_is_ptr(const void *ptr);
bool lowfat_is_heap_ptr(const void *ptr);
bool lowfat_is_stack_ptr(const void *ptr);
size_t lowfat_live_count(void);
extern uint8_t LOWFAT_HEAP[LOWFAT_NUM_REGIONS][LOWFAT_REGION_SIZE];
#endif
