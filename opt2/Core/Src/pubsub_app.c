/* Actual application logic in C. The middleware API is a narrow trusted gate.
 * No middleware/publisher/subscription pointers are controlled by the apps. */
#include "benchmark_config.h"
#include "worker_domain.h"
#include <stdint.h>
#if BENCH_SFI_ENABLED
#error "Mode 3 pending hook design: do not label uninstrumented C as SFI"
#endif
typedef struct {
    volatile int32_t message, result;
    volatile uint32_t processed,last_value,reserved;
    volatile uint32_t probe_target,probe_result;
    volatile uint32_t command;
} app_object_t;
extern uint32_t app_gate(uint32_t op);
extern uint32_t app_probe_read(uintptr_t address);
extern uint32_t app_probe_write(uintptr_t address,uint32_t value);
static int diagnostic(app_object_t *o)
{
    uint32_t command=o->command,value;
    if(command!=DOMAIN_READ && command!=DOMAIN_WRITE) return 0;
    value=command==DOMAIN_READ?app_probe_read((uintptr_t)(uint32_t)o->message):
        app_probe_write((uintptr_t)(uint32_t)o->message,(uint32_t)o->result);
    o->result=(int32_t)value;
    o->command=value==DOMAIN_BLOCKED?DOMAIN_DENIED:DOMAIN_DONE;
    return 1;
}
/* Application callback: runs in the SUB task, AFTER the syscall has returned
 * to unprivileged execution. This is the actual processing, not a copy helper. */
__attribute__((noinline)) void subscriber_app_callback(app_object_t *o)
{
    int32_t value=o->message;
    o->result=value;
    o->processed++;
    o->last_value=(uint32_t)value;
}
void publisher_app_task(void *argument)
{
    app_object_t *o=argument;
    app_gate(APP_WAIT); /* One bootstrap wait per task, not per message. */
    for(;;) {
        uint32_t control;
        __asm volatile("mrs %0, control" : "=r"(control));
        o->reserved=control;
        if(!diagnostic(o)) {
            o->message++; /* The publisher itself produces the next sample. */
            uint32_t status=app_gate(APP_PUBLISH);
            /* Back in the PUB domain: a real LDR of the registered SUB buffer.
             * The MPU remains enabled. Only the expected fault can recover. */
            o->probe_result=app_probe_read(o->probe_target);
            o->command=status==0?DOMAIN_DONE:DOMAIN_DENIED;
            app_gate(APP_DONE_WAIT);
        } else {
            app_gate(APP_DONE_WAIT); /* Diagnostics never publish. */
        }
    }
}
void subscriber_app_task(void *argument)
{
    app_object_t *o=argument;
    uint32_t status=app_gate(APP_WAIT); /* Bootstrap wait also receives first normal request. */
    for(;;) {
        uint32_t control;
        __asm volatile("mrs %0, control" : "=r"(control));
        o->reserved=control;
        if(!diagnostic(o)) {
            if(status==0) subscriber_app_callback(o);
            o->command=status==0?DOMAIN_DONE:DOMAIN_DENIED;
        }
        status=app_gate(APP_DONE_WAIT); /* Return with the NEXT request received. */
    }
}
