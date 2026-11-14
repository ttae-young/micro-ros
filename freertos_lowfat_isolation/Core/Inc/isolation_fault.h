#ifndef ISOLATION_FAULT_H
#define ISOLATION_FAULT_H
#include <stdint.h>
#include <stdbool.h>
/* Pure acceptance rule, shared with host negative tests. This helper does not
 * touch registers, change protection, or perform recovery. */
typedef struct {
    uintptr_t sp, stack_begin, stack_end, task, publisher_task;
    uint32_t exception, exc_return, armed, blocked, cfsr, hfsr;
    uint32_t fault_address, target, pc, load_pc, address_register, control, xpsr;
} isolation_fault_info_t;
static inline bool isolation_stack_valid(uintptr_t sp, uintptr_t begin, uintptr_t end)
{
    return end >= begin && end - begin >= 32U && sp >= begin &&
           sp <= end - 32U && (sp & 3U) == 0U;
}
static inline bool isolation_fault_matches(const isolation_fault_info_t *f)
{
    return isolation_stack_valid(f->sp, f->stack_begin, f->stack_end) &&
        f->exception == 4U && (f->exc_return & 0x1cU) == 0x1cU &&
        f->task == f->publisher_task && f->armed == 1U && f->blocked == 0U &&
        f->cfsr == 0x82U && f->hfsr == 0U &&
        f->fault_address == f->target && f->address_register == f->target &&
        (f->control & 3U) == 3U && (f->xpsr & 0x1ffU) == 0U &&
        (f->xpsr & 0x01000000U) != 0U && f->pc == (f->load_pc & ~1U);
}
#endif
