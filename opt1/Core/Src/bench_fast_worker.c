#include "benchmark_config.h"
#if BENCH_OPT3 && BENCH_MPU_ENABLED
#ifdef BENCH_WORKER_HOST_TEST
#include "worker_host_stubs.h"
#else
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "FreeRTOS.h"
#include "task.h"
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "bench_fast_worker.h"
#endif
/* These static restricted tasks are never deleted in this application.
 * Only privileged service code can execute these functions or read handles.
 * Unprivileged workers still enter normal MPU syscall wrappers to block. */
static TaskHandle_t worker_handles[BENCH_SUBSCRIBERS+1] PRIVILEGED_DATA;
bool bench_fast_create(unsigned slot, const TaskParameters_t *p)
{
    if (slot > BENCH_SUBSCRIBERS || !p || worker_handles[slot] ||
        !p->pvTaskCode || !p->puxStackBuffer || !p->pxTaskBuffer ||
        (p->uxPriority & portPRIVILEGE_BIT)) return false;
    return xTaskCreateRestrictedStatic(p,&worker_handles[slot]) == pdPASS && worker_handles[slot] != NULL;
}
bool bench_fast_notify(unsigned slot)
{
    if (slot > BENCH_SUBSCRIBERS || !worker_handles[slot]) return false;
    return xTaskGenericNotify(worker_handles[slot],0,0,eIncrement,NULL) == pdPASS;
}
#endif
