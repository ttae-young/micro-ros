#ifndef LOWFAT_CONFIG_STM32_H
#define LOWFAT_CONFIG_STM32_H

#include <stddef.h>
#include <stdint.h>

/*
 * STM32 포팅 초기 버전
 * 작은 allocation size만 지원
 */

#define LOWFAT_NUM_REGIONS       9
#define LOWFAT_REGION_SIZE       2048
#define LOWFAT_MIN_ALLOC_SIZE    16

extern const size_t lowfat_sizes[LOWFAT_NUM_REGIONS + 1];

size_t lowfat_heap_select(size_t size);

#endif
