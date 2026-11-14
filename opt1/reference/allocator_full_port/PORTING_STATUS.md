# LowFat STM32 FreeRTOS port

This directory is the board-oriented port. It retains the original LowFat
ideas that are meaningful on an STM32 MCU: size classes, constant-time region
lookup, object-base calculation, bump allocation, a LIFO free list, calloc,
realloc, selected alignment helpers, string duplication, and bounds queries.

Linux-only mechanisms are replaced as follows:

- `mmap(MAP_FIXED...)`: statically allocated, aligned `LOWFAT_HEAP` in SRAM.
- `pthread_mutex_t`: a statically allocated FreeRTOS mutex.
- `/dev/urandom` heap offset: omitted; the MCU linker owns the SRAM layout.
- `mprotect`, guard pages, and `madvise`: unavailable without an MMU. Runtime
  checks use `lowfat_check_access()` and exact requested-size metadata.
- `dlsym`, `__libc_malloc`, and `LD_PRELOAD` aliases: omitted. micro-ROS must
  explicitly install the allocator callbacks.
- constructor initialization: replaced by one explicit
  `lowfat_malloc_init()` call before micro-ROS initialization.
- unsupported allocations: return `NULL`; they never silently escape to an
  untracked libc heap.

`lowfat_check_access(pointer, length)` is required immediately before a direct
read or write that must be checked. Redirecting the micro-ROS allocator alone
does not instrument arbitrary C pointer dereferences.

Board source list:

1. `lowfat_mcu.c`
2. `lowfat_extra_mcu.c`
3. `lowfat_fallback_mcu.c`
4. `lowfat_config_stm32.c`
5. `microros_lowfat_allocator.c` in the micro-ROS firmware only

Include `lowfat_tests.c` and `board_main.c` only in the standalone validation
firmware. The micro-ROS firmware must call
`lowfat_install_microros_allocator()` before `rclc_support_init()`, node,
publisher, subscriber, or executor creation. A false return is a fatal startup
error because continuing would mix the LowFat pool with another allocator.
