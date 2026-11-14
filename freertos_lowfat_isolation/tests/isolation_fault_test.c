#include "isolation_fault.h"
#include <assert.h>
#include <stdio.h>
#define REJECT(field, value) do { isolation_fault_info_t b = good; b.field = (value); assert(!isolation_fault_matches(&b)); } while (0)
int main(void)
{
    isolation_fault_info_t good = {
        .sp=0x20001000, .stack_begin=0x20001000, .stack_end=0x20001400,
        .task=0x20002000, .publisher_task=0x20002000,
        .exception=4, .exc_return=0xfffffffd, .armed=1, .blocked=0,
        .cfsr=0x82, .hfsr=0, .fault_address=0x20031024, .target=0x20031024,
        .pc=0x08010006, .load_pc=0x08010006, .address_register=0x20031024,
        .control=3, .xpsr=0x01000000
    };
    assert(isolation_fault_matches(&good));
    REJECT(sp,0x20000ffc); REJECT(sp,0x200013e4); REJECT(sp,0x20001001);
    REJECT(stack_end,0x20001000); REJECT(stack_end,0x1000);
    REJECT(task,0x20002004); REJECT(exception,3); REJECT(exc_return,0xfffffff9);
    REJECT(exc_return,0xffffffed); REJECT(armed,0); REJECT(blocked,1);
    REJECT(cfsr,0x2); REJECT(cfsr,0x81); REJECT(cfsr,0x40082); REJECT(hfsr,0x40000000);
    REJECT(fault_address,0x20031020); REJECT(target,0x20031020);
    REJECT(address_register,0x20031020); REJECT(pc,0x08010008);
    REJECT(control,2); REJECT(control,1); REJECT(xpsr,0x01000004); REJECT(xpsr,0);
    good.sp=good.stack_end-32; assert(isolation_fault_matches(&good));
    puts("PASS: expected fault accepted; 23 wrong-context/fault/frame cases rejected");
    return 0;
}
