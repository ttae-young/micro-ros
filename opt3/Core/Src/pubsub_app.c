/* Actual application logic in C. The middleware API is a narrow trusted gate.
 * No middleware/publisher/subscription pointers are controlled by the apps. */
#include "benchmark_config.h"
#include "worker_domain.h"
#include <stdint.h>

#include "bench_payload.h"
extern uint32_t app_gate(uint32_t op);
extern uint32_t app_probe_read(uintptr_t address);
extern uint32_t app_probe_write(uintptr_t address,uint32_t value);
#if BENCH_SFI_ENABLED
extern uint32_t sfi_app_read32(uintptr_t address);
extern void sfi_app_write32(uintptr_t address,uint32_t value);
#define READ(o,f) sfi_app_read32((uintptr_t)&(o)->f)
#define WRITE(o,f,v) sfi_app_write32((uintptr_t)&(o)->f,(uint32_t)(v))
#else
#define READ(o,f) ((o)->f)
#define WRITE(o,f,v) ((o)->f=(v))
#endif
/* Field addresses are computed in C. MODE3 dereferences only in checked hooks.
 * Build verification rejects raw app loads/stores except bounded stack locals.
 */
__attribute__((noinline,noclone)) static int diagnostic(volatile app_object_t *o)
{
    uint32_t command=READ(o,command),value;
    if(command!=DOMAIN_READ && command!=DOMAIN_WRITE) return 0;
    value=command==DOMAIN_READ?app_probe_read((uintptr_t)(uint32_t)READ(o,payload[0])):
        app_probe_write((uintptr_t)(uint32_t)READ(o,payload[0]),(uint32_t)READ(o,result));
    WRITE(o,result,value);
    WRITE(o,command,value==DOMAIN_BLOCKED?DOMAIN_DENIED:DOMAIN_DONE);
    return 1;
}
__attribute__((noinline)) void subscriber_app_callback(volatile app_object_t *o)
{
    uint32_t value=READ(o,payload[0]);
    uint32_t mismatch=0;
#if BENCH_CHECK_ALL_WORDS
    for (uint32_t i=1; i<BENCH_PAYLOAD_WORDS; ++i)
        mismatch |= READ(o,payload[i]) ^ (value+i);
#endif
    WRITE(o,result,value);
    WRITE(o,processed,READ(o,processed)+1U);
    WRITE(o,last_value,value ^ mismatch);
}
void publisher_app_task(void *argument)
{
    volatile app_object_t *o=argument;
    app_gate(APP_WAIT);
    for(;;) {
        uint32_t control;
        __asm volatile("mrs %0, control" : "=r"(control));
        WRITE(o,reserved,control);
        if(!diagnostic(o)) {
            uint32_t sequence=READ(o,payload[0])+1U;
            for (uint32_t i=0; i<BENCH_PAYLOAD_WORDS; ++i)
                WRITE(o,payload[i],sequence+i);
            uint32_t status=app_gate(APP_PUBLISH);
            if(BENCH_DIRECT_READ) {
                uint32_t value=app_probe_read(READ(o,probe_target));
                WRITE(o,probe_result,value);
            }
            WRITE(o,command,status==0?DOMAIN_DONE:DOMAIN_DENIED);
            app_gate(APP_DONE_WAIT);
        } else {
            app_gate(APP_DONE_WAIT);
        }
    }
}
void subscriber_app_task(void *argument)
{
    volatile app_object_t *o=argument;
    uint32_t status=app_gate(APP_WAIT);
    for(;;) {
        uint32_t control;
        __asm volatile("mrs %0, control" : "=r"(control));
        WRITE(o,reserved,control);
        if(!diagnostic(o)) {
            if(status==0) subscriber_app_callback(o);
            WRITE(o,command,status==0?DOMAIN_DONE:DOMAIN_DENIED);
        }
        status=app_gate(APP_DONE_WAIT);
    }
}
