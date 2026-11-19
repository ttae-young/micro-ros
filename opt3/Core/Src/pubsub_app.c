/* Actual application logic in C. The middleware API is a narrow trusted gate.
 * No middleware/publisher/subscription pointers are controlled by the apps. */
#include "benchmark_config.h"
#include "worker_domain.h"
#include <stdint.h>

typedef struct {
    volatile int32_t message, result;
    volatile uint32_t processed,last_value,reserved;
    volatile uint32_t probe_target,probe_result;
    volatile uint32_t command;
} app_object_t;
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
__attribute__((noinline,noclone)) static int diagnostic(app_object_t *o)
{
    uint32_t command=READ(o,command),value;
    if(command!=DOMAIN_READ && command!=DOMAIN_WRITE) return 0;
    value=command==DOMAIN_READ?app_probe_read((uintptr_t)(uint32_t)READ(o,message)):
        app_probe_write((uintptr_t)(uint32_t)READ(o,message),(uint32_t)READ(o,result));
    WRITE(o,result,value);
    WRITE(o,command,value==DOMAIN_BLOCKED?DOMAIN_DENIED:DOMAIN_DONE);
    return 1;
}
__attribute__((noinline)) void subscriber_app_callback(app_object_t *o)
{
    int32_t value=READ(o,message);
    WRITE(o,result,value);
    WRITE(o,processed,READ(o,processed)+1U);
    WRITE(o,last_value,(uint32_t)value);
}
void publisher_app_task(void *argument)
{
    app_object_t *o=argument;
    app_gate(APP_WAIT);
    for(;;) {
        uint32_t control;
        __asm volatile("mrs %0, control" : "=r"(control));
        WRITE(o,reserved,control);
        if(!diagnostic(o)) {
            WRITE(o,message,READ(o,message)+1U);
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
    app_object_t *o=argument;
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
