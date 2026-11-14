#ifndef MICROROS_LOWFAT_ALLOCATOR_H
#define MICROROS_LOWFAT_ALLOCATOR_H

#include <stdbool.h>
#include "rcutils/allocator.h"

rcutils_allocator_t lowfat_rcutils_allocator(void);
bool lowfat_install_microros_allocator(void);

#endif
