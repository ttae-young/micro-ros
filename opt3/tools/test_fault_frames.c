#include "isolation_fault.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    isolation_fault_info_t f={
        .sp=0x20001000,.stack_begin=0x20001000,.stack_end=0x20001400,
        .task=0x1234,.publisher_task=0x1234,.exception=4,.exc_return=0xffffffbc,
        .armed=1,.blocked=0,.cfsr=0x82,.hfsr=0,.fault_address=0x20002000,
        .target=0x20002000,.pc=0x8001000,.load_pc=0x8001001,
        .address_register=0x20002000,.control=3,.xpsr=0x01000000
    };
    assert(isolation_fault_matches(&f));
    f.exc_return=0xffffffac; f.control=7;
    assert(isolation_fault_matches(&f));
    f.stack_end=f.sp+103; assert(!isolation_fault_matches(&f));
    f.stack_end=f.sp+104; assert(isolation_fault_matches(&f));
    f.xpsr|=0x200; assert(!isolation_fault_matches(&f));
    f.stack_end=f.sp+108; assert(isolation_fault_matches(&f));
    f.task++; assert(!isolation_fault_matches(&f)); f.task--;
    f.pc+=2; assert(!isolation_fault_matches(&f)); f.pc-=2;
    f.target++; assert(!isolation_fault_matches(&f)); f.target--;
    f.exc_return&=~8U; assert(!isolation_fault_matches(&f)); f.exc_return|=8U;
    f.cfsr|=0x40000; assert(!isolation_fault_matches(&f)); f.cfsr=0x82;
    f.control=2; assert(!isolation_fault_matches(&f)); f.control=3;
    f.armed=0; assert(!isolation_fault_matches(&f));
    puts("PASS basic/FP frames, alignment pad, stack bounds and wrong-fault rejection");
}
