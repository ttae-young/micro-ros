/* Core experiment: normal micro-ROS communication and actual unprivileged
 * PUB -> SUB reads. A direct SUB-buffer read and its expected fault are INSIDE every iteration.
 * Objects/tasks have static lifetime; no message batching or privilege widening. */
#include "main.h"
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "FreeRTOS.h"
#include "task.h"
#include "SEGGER_RTT.h"
#include "freertos_mpu_demo.h"
#include "benchmark_config.h"
#include "bench_allocator.h"
#include "lowfat_malloc.h"
#include "bench_fast_worker.h"
#include "isolation_fault.h"
#include "worker_domain.h"
#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <rcutils/allocator.h>
#include <std_msgs/msg/u_int32_multi_array.h>
#include "bench_payload.h"
#include <uxr/client/transport.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
extern int clock_gettime(int clock_id, struct timespec *tp);



#define WORKER_STACK_WORDS 1024U
#define MICROROS_STACK_WORDS 4096U
typedef struct { uint64_t sum; uint32_t n, min, max; } statistics_t;
extern UART_HandleTypeDef hlpuart1;
bool cubemx_transport_open(struct uxrCustomTransport *);
bool cubemx_transport_close(struct uxrCustomTransport *);
size_t cubemx_transport_write(struct uxrCustomTransport *, const uint8_t *, size_t, uint8_t *);
size_t cubemx_transport_read(struct uxrCustomTransport *, uint8_t *, size_t, int, uint8_t *);
void *microros_allocate(size_t, void *);
void microros_deallocate(void *, void *);
void *microros_reallocate(void *, size_t, void *);
void *microros_zero_allocate(size_t, size_t, void *);

static statistics_t g_bounds_stats, g_iteration_stats;
static uint32_t g_init_cycles, g_ros_alloc_calls;
static void create_workers(void);
static app_object_t *g_pub;
static app_object_t *g_sub[BENCH_SUBSCRIBERS];
/* Workers own payloads, NOT pointer-bearing middleware handles. A corrupted
 * payload cannot redirect privileged rcl calls through a forged handle. */
static rcl_node_t g_pub_node PRIVILEGED_DATA;
static std_msgs__msg__UInt32MultiArray g_pub_wire PRIVILEGED_DATA;
static std_msgs__msg__UInt32MultiArray g_sub_wire[BENCH_SUBSCRIBERS] PRIVILEGED_DATA;
static rcl_publisher_t g_publisher PRIVILEGED_DATA;
static rcl_subscription_t g_subscriptions[BENCH_SUBSCRIBERS] PRIVILEGED_DATA;
static rcl_node_t g_sub_node;
static StackType_t g_sub_stacks[BENCH_SUBSCRIBERS][WORKER_STACK_WORDS] __attribute__((aligned(32)));
static StaticTask_t g_sub_tcbs[BENCH_SUBSCRIBERS];
#if !(BENCH_OPT3 && BENCH_MPU_ENABLED)
static TaskHandle_t g_sub_workers[BENCH_SUBSCRIBERS];
#endif
static StackType_t g_worker_stack[WORKER_STACK_WORDS] __attribute__((aligned(32)));
static StaticTask_t g_worker_tcb;
#if !(BENCH_OPT3 && BENCH_MPU_ENABLED)
static TaskHandle_t g_worker;
#endif
static rclc_support_t g_support;
static rclc_executor_t g_executors[BENCH_SUBSCRIBERS];

static bool g_rx_ready[BENCH_SUBSCRIBERS];
static TaskHandle_t g_controller;
static volatile int g_active = -1;
static volatile bool g_done;
static uint32_t g_gate_pub_calls, g_gate_sub_calls, g_gate_normal_done;
static uint32_t g_expected_sequence;
static uint32_t g_app_cycles[BENCH_SUBSCRIBERS+1];
static uint32_t g_app_start[BENCH_SUBSCRIBERS+1];
static uint32_t g_middleware_cycles[BENCH_SUBSCRIBERS+1];
static statistics_t g_app_stats[BENCH_SUBSCRIBERS+1];
static statistics_t g_app_total_stats;
static statistics_t g_dispatch_stats[BENCH_SUBSCRIBERS+1],g_dispatch_sub_stats,g_dispatch_all_stats;
static statistics_t g_receive_internal_stats,g_middleware_total_stats;
static uint32_t g_measured_io_calls,g_measured_done_wait_calls;
static uint32_t g_measured_gate_entries;
static bool g_bootstrap_seen[BENCH_SUBSCRIBERS+1];
static statistics_t g_publish_stats, g_worker_stats, g_e2e_stats;
static statistics_t g_sub_worker_stats[BENCH_SUBSCRIBERS], g_sub_batch_stats;
static uint32_t g_seen, g_sub_received[BENCH_SUBSCRIBERS];
static bool g_measured;
#define ALL_SUBS_MASK ((1U << BENCH_SUBSCRIBERS) - 1U)
static uint32_t g_layout_cycles, g_timer_cycles;
static uint32_t g_publish_errors, g_timeouts, g_unexpected_rx, g_worker_errors, g_spin_errors;
static int32_t g_sequence;
static bool g_received;

static void fatal(const char *stage, int32_t code)
{
    bench_allocator_stats_t a = bench_allocator_stats();
    SEGGER_RTT_printf(0, "\n[BENCH ERROR] %s (%ld) allocator_failures=%lu last_request=%lu\n",
        stage, (long)code, (unsigned long)a.failures, (unsigned long)a.last_failed_request);
    for (;;) { __NOP(); }
}
#define CHECK_RCL(call, stage) do { rcl_ret_t rc_ = (call); \
    if (rc_ != RCL_RET_OK) fatal(stage, rc_); } while (0)
/* Privileged caller only. Elapsed cycles include interrupts and waiting. */
static uint32_t cycles(void)
{
    __DSB(); __ISB();
    return DWT->CYCCNT;
}
static void clock_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) fatal("DWT unavailable", 0);
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t before = cycles();
    for (volatile uint32_t i = 0; i < 32U; ++i) { __NOP(); }
    if (cycles() == before) fatal("DWT not running", 0);
    before = cycles();
    g_timer_cycles = cycles() - before;
}
static void add(statistics_t *s, uint32_t value)
{
    if (s->n == 0U || value < s->min) s->min = value;
    if (value > s->max) s->max = value;
    s->sum += value;
    s->n++;
}
static void decimal64(uint64_t n, char *s)
{
    char t[24]; unsigned k=0; do { t[k++]=(char)('0'+n%10); n/=10; } while(n);
    unsigned j=0; while(k) s[j++]=t[--k]; s[j]=0;
}
static void print_stat(const char *name, const statistics_t *s)
{
    uint32_t mean=s->n?(uint32_t)(s->sum/s->n):0;
    uint64_t mean_ns=s->n?((s->sum/SystemCoreClock)*1000000000ULL+
        ((s->sum%SystemCoreClock)*1000000000ULL)/SystemCoreClock)/s->n:0;
    uint64_t total_ns=(s->sum/SystemCoreClock)*1000000000ULL+
        ((s->sum%SystemCoreClock)*1000000000ULL)/SystemCoreClock;
    char sum[24],total_us[24]; decimal64(s->sum,sum); decimal64(total_ns/1000,total_us);
    SEGGER_RTT_printf(0,"CSV,%d,%d,%s,%lu,%lu,%lu,%lu,%lu.%03lu,%s,%s.%03lu\n",
        BENCH_MODE,BENCH_SUBSCRIBERS,name,(unsigned long)s->n,(unsigned long)s->min,
        (unsigned long)mean,(unsigned long)s->max,(unsigned long)(mean_ns/1000),
        (unsigned long)(mean_ns%1000),sum,total_us,(unsigned long)(total_ns%1000));
}
/* Explicit checks at the service-to-worker boundary, not compiler instrumentation. */
static void guard(const void *p, size_t n, bool measured)
{
#if BENCH_LOWFAT_ENABLED
    uint32_t start = cycles();
    bool ok = lowfat_check_access(p, n);
    uint32_t elapsed = cycles() - start;
    if (!ok) fatal("LowFat bounds check", (int32_t)n);
    if (measured) add(&g_bounds_stats, elapsed);
#else
    (void)p; (void)n; (void)measured;
#endif
}
static void allocator_selftest(void)
{
#if BENCH_LOWFAT_ENABLED
    unsigned char *p = lowfat_malloc(17);
    if (!p || lowfat_size(p) != 32 || lowfat_base(p + 16) != p ||
        !lowfat_check_access(p, 17) || lowfat_check_access(p + 16, 2))
        fatal("LowFat bounds selftest", 0);
    memset(p, 0x5a, 17);
    unsigned char *q = lowfat_realloc(p, 80);
    if (!q) fatal("LowFat realloc selftest", 0);
    for (unsigned i = 0; i < 17; ++i)
        if (q[i] != 0x5a) fatal("LowFat preserved data", 0);
    if (!lowfat_free_checked(q) || lowfat_free_checked(q))
        fatal("LowFat free selftest", 0);
    SEGGER_RTT_WriteString(0, "[LOWFAT SELFTEST] base/size/bounds/realloc/free PASS\n");
#endif
}
/* C app bodies live in pubsub_app.c; worker_domain.S supplies the fixed gate
 * and exact faulting access sites. Build validation checks the gate veneer. */
typedef struct {
    volatile uint32_t armed, blocked, target, fault_pc, cfsr, control;
    uintptr_t task, stack_begin, stack_end, expected_pc, recovery_pc;
} probe_state_t;
static probe_state_t g_probe PRIVILEGED_DATA;
static volatile uint32_t g_policy_canary PRIVILEGED_DATA = 0x24681357U;
static uint32_t g_probe_passes, g_policy_passes;
/* Privileged bookkeeping: app data cannot authorize an arbitrary fault. */
static bool g_direct_pending;
static uint32_t g_direct_index,g_direct_original;
static uint32_t g_direct_total,g_direct_measured,g_direct_blocked;
static uint32_t g_direct_targets[BENCH_SUBSCRIBERS];
static void prepare_direct_read(void)
{
    if(g_direct_pending || g_probe.armed || SCB->CFSR || SCB->HFSR)
        fatal("stale direct read",0);
    g_direct_index=(g_expected_sequence-1U)%BENCH_SUBSCRIBERS;
    g_direct_original=(uint32_t)g_sub[g_direct_index]->payload[0];
    g_probe.blocked=g_probe.fault_pc=g_probe.cfsr=g_probe.control=0;
    g_probe.target=(uintptr_t)&g_sub[g_direct_index]->payload[0];
    g_probe.task=(uintptr_t)&g_worker_tcb;
    g_probe.stack_begin=(uintptr_t)g_worker_stack;
    g_probe.stack_end=(uintptr_t)(g_worker_stack+WORKER_STACK_WORDS);
    g_probe.expected_pc=(uintptr_t)pub_load_site;
    g_probe.recovery_pc=(uintptr_t)pub_commit_site;
    g_pub->probe_target=g_probe.target;
    g_pub->probe_result=0xdeadbeefU;
    g_direct_pending=true;
    g_probe.armed=BENCH_MPU_ENABLED;
    __DMB();
}
static void finish_direct_read(void)
{
    __DMB();
    bool ok=g_pub->probe_target==g_probe.target &&
        (uint32_t)g_sub[g_direct_index]->payload[0]==g_direct_original &&
        SCB->CFSR==0 && SCB->HFSR==0;
    if(BENCH_MPU_ENABLED || BENCH_SFI_ENABLED)
        ok=ok && g_probe.blocked==1 && !g_probe.armed && g_pub->probe_result==DOMAIN_BLOCKED;
    else ok=ok && !g_probe.blocked && g_pub->probe_result==g_direct_original;
    if(!ok) fatal("periodic direct read",g_direct_index);
    g_probe.armed=0; g_direct_pending=false; ++g_direct_total;
    if(g_measured) {
        ++g_direct_measured; ++g_direct_targets[g_direct_index];
        g_direct_blocked+=g_probe.blocked;
    }
}


static void notify_worker(uint32_t i)
{
    if (g_active != -1 || ulTaskNotifyTake(pdTRUE,0)!=0) fatal("stale completion",i);
    g_active=(int)i; g_done=false;
    g_app_cycles[i]=g_middleware_cycles[i]=0;
    xTaskNotifyGive(i==0 ? g_worker : g_sub_workers[i-1]);
    if (ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(BENCH_RX_TIMEOUT_MS+2000U))!=1 || !g_done)
        fatal("application completion timeout",i);
    g_active=-1;
}
static void notify_publisher(void) { notify_worker(0); }
static app_object_t *worker_object(uint32_t i) { return i == 0 ? g_pub : g_sub[i-1]; }
static StackType_t *worker_stack(uint32_t i) { return i == 0 ? g_worker_stack : g_sub_stacks[i-1]; }
static StaticTask_t *worker_tcb(uint32_t i) { return i == 0 ? &g_worker_tcb : &g_sub_tcbs[i-1]; }
/* Closed-app SFI trusted runtime. Native app code cannot access this metadata:
 * post-link verification admits only stack-local loads/stores and these hooks.
 * Hook identity comes from the actual TCB, not an app-supplied owner/address. */
#if BENCH_SFI_ENABLED
static uint32_t g_sfi_checks,g_sfi_denials;
static bool sfi_allowed(uintptr_t address,bool write)
{
    int who=g_active;
    if(who<0 || who>BENCH_SUBSCRIBERS ||
       xTaskGetCurrentTaskHandle()!=(TaskHandle_t)worker_tcb((unsigned)who))
        fatal("SFI caller authentication",who);
    if(g_measured) ++g_sfi_checks;
    if(address&3U) return false;
    uintptr_t object=(uintptr_t)worker_object((unsigned)who);
    if(address>=object && address-object<=BENCH_OBJECT_BYTES-4U) return true;
    uintptr_t lo=(uintptr_t)worker_stack((unsigned)who);
    uintptr_t hi=lo+WORKER_STACK_WORDS*sizeof(StackType_t);
    if(address<lo || address>hi-4U) return false;
    /* SFI-specific CFI rule: dynamic writes may not touch active call frames.
     * Compiler-generated stack-local accesses are statically checked instead.
     * This also protects saved registers and return addresses of trusted calls. */
    if(write) {
        uintptr_t sp=__get_PSP();
        if(sp<lo || sp>hi || address>sp-4U) return false;
    }
    return true;
}
__attribute__((noinline)) uint32_t sfi_app_read32(uintptr_t address)
{
    if(!sfi_allowed(address,false)) fatal("unchecked app read rejected",0);
    return *(volatile uint32_t *)address;
}
__attribute__((noinline)) void sfi_app_write32(uintptr_t address,uint32_t value)
{
    if(!sfi_allowed(address,true)) fatal("unchecked app write rejected",0);
    *(volatile uint32_t *)address=value;
}
static uint32_t sfi_reject(uintptr_t address)
{
    /* Deny before dereferencing. Reporting does not grant an access. */
    if(address!=g_probe.target || g_probe.task!=(uintptr_t)xTaskGetCurrentTaskHandle() || g_probe.blocked)
        fatal("unexpected SFI violation",0);
    g_probe.blocked=1;
    if(g_measured) ++g_sfi_denials;
    return DOMAIN_BLOCKED;
}
__attribute__((noinline)) uint32_t sfi_probe_read(uintptr_t address)
{
    if(!sfi_allowed(address,false)) return sfi_reject(address);
    return *(volatile uint32_t *)address;
}
__attribute__((noinline)) uint32_t sfi_probe_write(uintptr_t address,uint32_t value)
{
    if(!sfi_allowed(address,true)) return sfi_reject(address);
    *(volatile uint32_t *)address=value;
    return DOMAIN_WRITE_OK;
}
#endif
/* Trusted diagnostic driver. Only the selected worker performs the access.
 * Rejection/expected fault recovery is OUTSIDE all timed windows. */
static uint32_t probe(uint32_t who, volatile uint32_t *target, bool write, bool foreign)
{
    volatile app_object_t *o=worker_object(who);
    uint32_t saved_msg=o->payload[0], saved_result=o->result, saved_command=o->command;
    uint32_t original=*target;
    bool deny=foreign && (BENCH_MPU_ENABLED || BENCH_SFI_ENABLED);
    if (SCB->CFSR || SCB->HFSR) fatal("stale fault", 0);
    g_probe.blocked=g_probe.fault_pc=g_probe.cfsr=g_probe.control=0;
    g_probe.target=(uintptr_t)target;
    g_probe.task=(uintptr_t)worker_tcb(who);
    g_probe.stack_begin=(uintptr_t)worker_stack(who);
    g_probe.stack_end=g_probe.stack_begin+WORKER_STACK_WORDS*sizeof(StackType_t);
    g_probe.expected_pc=(uintptr_t)(who==0 ? (write ? pub_store_site : pub_load_site) : (write ? sub_store_site : sub_load_site));
    g_probe.recovery_pc=(uintptr_t)(who==0 ? pub_commit_site : sub_commit_site);
    o->payload[0]=(uintptr_t)target;
    o->result=0x13572468U;
    o->command=write ? DOMAIN_WRITE : DOMAIN_READ;
    g_probe.armed=deny && BENCH_MPU_ENABLED;
    __DMB();
    notify_worker(who);
    __DMB();
    g_probe.armed=0;
    uint32_t status=o->command, value=o->result;
    bool ok=deny ? (status==DOMAIN_DENIED && value==DOMAIN_BLOCKED && *target==original) :
        (status==DOMAIN_DONE && (write ? (*target==0x13572468U && value==DOMAIN_WRITE_OK) : value==original));
    if ((BENCH_MPU_ENABLED || BENCH_SFI_ENABLED) && deny) ok=ok && g_probe.blocked==1;
    else ok=ok && g_probe.blocked==0 && SCB->CFSR==0 && SCB->HFSR==0;
    /* Only safe allocated diagnostic words are ever written; restore immediately. */
    if (write && !deny) *target=original;
    o->payload[0]=saved_msg; o->result=saved_result; o->command=saved_command;
    if (!ok) fatal("worker policy probe", (int32_t)who);
    return value;
}
static void isolation_test(const char *phase)
{
    for (uint32_t i=0; i<BENCH_SUBSCRIBERS; ++i) {
        uint32_t value=probe(0,(volatile uint32_t *)&g_sub[i]->payload[0],false,true);
        SEGGER_RTT_printf(0,"[ISOLATION %s] PUB -> SUB%u address=%p %s PASS\n",phase,i+1U,
            (void *)&g_sub[i]->payload[0], BENCH_MPU_ENABLED ? "BLOCKED BY MPU" :
            (BENCH_SFI_ENABLED ? "BLOCKED BY APP SFI" : "READ ALLOWED"));
        if (!(BENCH_MPU_ENABLED || BENCH_SFI_ENABLED)) SEGGER_RTT_printf(0,"  value=%lu\n",(unsigned long)value);
        ++g_probe_passes;
    }
}
/* Raw kernel getter is used only by this privileged service, never by workers. */
extern UBaseType_t policy_stack_watermark(TaskHandle_t) __asm__("uxTaskGetStackHighWaterMark");
static void policy_test(void)
{
    for (uint32_t who=0; who<=BENCH_SUBSCRIBERS; ++who) {
        if (policy_stack_watermark((TaskHandle_t)worker_tcb(who)) < 32U)
            fatal("insufficient worker stack probe margin",(int32_t)who);
    }
    for (uint32_t who=0; who<=BENCH_SUBSCRIBERS; ++who) {
        /* Reserved +8 is ordinary payload data, with no kernel/middleware
         * pointers. Stack probes use a checked unused bottom-stack word. */
        probe(who,&worker_object(who)->processed,false,false);
        probe(who,&worker_object(who)->processed,true,false);
        probe(who,(volatile uint32_t *)(worker_stack(who)+16),false,false);
        probe(who,(volatile uint32_t *)(worker_stack(who)+16),true,false);
        probe(who,&g_policy_canary,false,true);
        probe(who,&g_policy_canary,true,true);
        for (uint32_t peer=0; peer<=BENCH_SUBSCRIBERS; ++peer) if (peer!=who) {
            probe(who,&worker_object(peer)->processed,false,true);
            probe(who,&worker_object(peer)->processed,true,true);
            probe(who,(volatile uint32_t *)(worker_stack(peer)+16),false,true);
            probe(who,(volatile uint32_t *)(worker_stack(peer)+16),true,true);
        }
        ++g_policy_passes;
        SEGGER_RTT_printf(0,"[POLICY] app=%lu own_object/stack=ALLOW other_object/stack/kernel=%s READ+WRITE PASS\n",
            (unsigned long)who,(BENCH_MPU_ENABLED || BENCH_SFI_ENABLED) ? "DENY" : "ALLOW");
    }
}
/* Transport adapter only: application callback executes AFTER leaving the gate,
 * in the subscriber task's unprivileged domain. It is in pubsub_app.c. */
static void transport_callback(const void *message)
{
    for(unsigned i=0;i<BENCH_SUBSCRIBERS;i++) if(message==&g_sub_wire[i]) {
        if(g_sub_wire[i].data.size != BENCH_PAYLOAD_WORDS || g_sub_wire[i].data.data != g_sub[i]->payload)
            fatal("received payload length or storage",i);
        if(g_rx_ready[i]) fatal("duplicate transport callback",i);
        g_rx_ready[i]=true; return;
    }
    fatal("unknown transport buffer",0);
}
uint32_t app_gate_impl(uint32_t op) PRIVILEGED_FUNCTION;
uint32_t app_gate_impl(uint32_t op)
{
    TaskHandle_t current=xTaskGetCurrentTaskHandle();
    unsigned who=BENCH_SUBSCRIBERS+1;
#if BENCH_TUNE_STAGE >= 4
    /* Controller-owned active index is only a candidate, never caller authority.
     * Authenticate against the actual current TCB on EVERY request. */
    int active=g_active;
    if(active>=0 && active<=BENCH_SUBSCRIBERS) {
        if(current==(TaskHandle_t)worker_tcb((unsigned)active)) who=(unsigned)active;
    } else if(op==APP_WAIT) {
        /* Bootstrap only: apps may block before any request is active. */
        for(unsigned i=0;i<=BENCH_SUBSCRIBERS;i++)
            if(current==(TaskHandle_t)worker_tcb(i)) { who=i; break; }
    }
#else
    for(unsigned i=0;i<=BENCH_SUBSCRIBERS;i++)
        if(current==(TaskHandle_t)worker_tcb(i)) { who=i; break; }
#endif
    if(who>BENCH_SUBSCRIBERS) fatal("unauthorized gate caller",op);
    volatile app_object_t *o=worker_object(who);
    if(op==APP_RECEIVE) fatal("separate receive forbidden",who);
    if(g_measured) ++g_measured_gate_entries; /* Actual app entry, never a wake-up. */
    if(op==APP_WAIT) {
        if(g_bootstrap_seen[who]) fatal("duplicate bootstrap wait",who);
        g_bootstrap_seen[who]=true;
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        if(g_active!=(int)who) fatal("wrong task awakened",who);
        g_app_start[who]=cycles();
        if(who==0 || o->command==DOMAIN_READ || o->command==DOMAIN_WRITE) return 0;
        /* Same authenticated SUB task resumes in its protected syscall stack.
         * Diagnostic requests must never receive a message. */
        op=APP_RECEIVE;
        goto dispatch_io;
    }
    if(g_active!=(int)who || g_done) fatal("gate outside request",op);
    if(op==APP_DONE_WAIT) {
        if(who==0 && g_direct_pending) finish_direct_read();
        if(g_measured) ++g_measured_done_wait_calls;
        if ((o->reserved&1U)!=(BENCH_MPU_ENABLED?1U:0U)) fatal("wrong application privilege",who);
        g_app_cycles[who]=cycles()-g_app_start[who]-g_middleware_cycles[who];
        if(o->command==DOMAIN_DONE && o->payload[0]==g_expected_sequence) ++g_gate_normal_done;
        g_done=true;
        xTaskNotifyGive(g_controller);
        /* Stay in the SAME syscall while blocked. Next request resumes here;
         * only then return to the unprivileged application. */
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        if(g_active!=(int)who || g_done) fatal("wrong fused wakeup",who);
        g_app_start[who]=cycles();
        if(who==0 || o->command==DOMAIN_READ || o->command==DOMAIN_WRITE) return 0;
        /* Same authenticated SUB task resumes in its protected syscall stack.
         * Diagnostic requests must never receive a message. */
        op=APP_RECEIVE;
        goto dispatch_io;
    }
dispatch_io:
    if(o->command!=DOMAIN_NORMAL) fatal("communication during diagnostic",op);
    if(g_measured) ++g_measured_io_calls;
    /* One-time check in the real caller's syscall context; warmup only. */
    static bool clock_checked[BENCH_SUBSCRIBERS+1];
    if(!clock_checked[who]) {
        struct timespec before,after;
        clock_gettime(0,&before);
        vTaskDelay(pdMS_TO_TICKS(2));
        clock_gettime(0,&after);
        if(after.tv_sec<before.tv_sec || (after.tv_sec==before.tv_sec && after.tv_nsec<=before.tv_nsec))
            fatal("middleware clock does not advance",who);
        clock_checked[who]=true;
        SEGGER_RTT_printf(0,"[CLOCK] app=%u syscall_clock_advances=PASS\n",who);
    }
    uint32_t start=cycles();
    rcl_ret_t rc;
    if(op==APP_PUBLISH && who==0) {
        ++g_gate_pub_calls;
        /* Publisher application supplied its OWN payload by writing its object.
         * Gate has no caller-provided pointer, handle, index, or callback. */
        rc=rcl_publish(&g_publisher,&g_pub_wire,NULL);
        uint32_t elapsed=cycles()-start;
        g_middleware_cycles[who]+=elapsed;
        if(g_measured) add(&g_publish_stats,elapsed);
        if(rc!=RCL_RET_OK) ++g_publish_errors;
        o->result=rc==RCL_RET_OK?0U:1U;
        if(BENCH_DIRECT_READ) prepare_direct_read();
        /* Ordinary publish return: the actual PUB application now reads SUB. */
        return o->result;
    }
    if(op==APP_RECEIVE && who>0) {
        unsigned i=who-1; ++g_gate_sub_calls;
        g_rx_ready[i]=false;
        TickType_t tick=xTaskGetTickCount();
        do {
            rc=rclc_executor_spin_some(&g_executors[i],RCL_MS_TO_NS(1));
            if(rc!=RCL_RET_OK && rc!=RCL_RET_TIMEOUT) { ++g_spin_errors; break; }
        } while(!g_rx_ready[i] && (TickType_t)(xTaskGetTickCount()-tick)<pdMS_TO_TICKS(BENCH_RX_TIMEOUT_MS));
        bool ok=g_rx_ready[i] && (TickType_t)(xTaskGetTickCount()-tick)<pdMS_TO_TICKS(BENCH_RX_TIMEOUT_MS);
        if(!ok) ++g_timeouts;
        g_middleware_cycles[who]+=cycles()-start;
        return ok?0:1;
    }
    fatal("gate role/op violation",op); return 1;
}
static void run_one(bool measured)
{
    uint32_t iteration_start=cycles();
    g_measured=measured; g_received=false; g_seen=0;
    g_expected_sequence=++g_sequence;
    /* Controller requests a round but NEVER constructs the publisher payload. */
    g_pub->command=DOMAIN_NORMAL;
    if(g_sequence==1) SEGGER_RTT_WriteString(0,"[WARMUP 1] PUB start\n");
    uint32_t start=cycles(); notify_publisher();
    if(g_sequence==1) SEGGER_RTT_WriteString(0,"[WARMUP 1] PUB done\n");
    uint32_t elapsed=cycles()-start;
    if(g_pub->command!=DOMAIN_DONE || g_pub->payload[0]!=g_sequence || g_pub->result!=0)
        ++g_worker_errors;
    if(measured) { add(&g_worker_stats,elapsed); add(&g_app_stats[0],g_app_cycles[0]); }
    uint32_t app_total=g_app_cycles[0],sub_total=0;
    uint32_t dispatch_pub=elapsed-g_middleware_cycles[0],dispatch_sub=0,receive_internal=0;
    if(measured) add(&g_dispatch_stats[0],dispatch_pub);
    for(unsigned i=0;i<BENCH_SUBSCRIBERS;i++) {
        g_sub[i]->command=DOMAIN_NORMAL;
        if(g_sequence==1) SEGGER_RTT_printf(0,"[WARMUP 1] SUB%u start\n",i+1);
        start=cycles(); notify_worker(i+1); elapsed=cycles()-start;
        if(g_sequence==1) SEGGER_RTT_printf(0,"[WARMUP 1] SUB%u done\n",i+1);
        if(g_sub[i]->command!=DOMAIN_DONE || g_sub[i]->result!=g_sequence ||
           g_sub[i]->processed!=(uint32_t)g_sequence || g_sub[i]->last_value!=(uint32_t)g_sequence)
            ++g_worker_errors;
        else g_seen|=1U<<i;
        sub_total+=elapsed; app_total+=g_app_cycles[i+1];
        uint32_t dispatch=elapsed-g_middleware_cycles[i+1]; dispatch_sub+=dispatch;
        receive_internal+=g_middleware_cycles[i+1];
        if(measured) add(&g_dispatch_stats[i+1],dispatch);
        if(measured) { add(&g_sub_worker_stats[i],elapsed); add(&g_app_stats[i+1],g_app_cycles[i+1]);
            ++g_sub_received[i]; }
    }
    g_received=g_seen==ALL_SUBS_MASK;
    if(measured) {
        add(&g_sub_batch_stats,sub_total); add(&g_app_total_stats,app_total);
        add(&g_dispatch_sub_stats,dispatch_sub); add(&g_dispatch_all_stats,dispatch_pub+dispatch_sub);
        add(&g_receive_internal_stats,receive_internal); add(&g_middleware_total_stats,g_middleware_cycles[0]+receive_internal);
        add(&g_e2e_stats,cycles()-iteration_start); add(&g_iteration_stats,cycles()-iteration_start);
    }
    g_measured=false;
    /* Verify even the first-word workload, after both elapsed-time windows close. */
    for (unsigned i=0; i<BENCH_SUBSCRIBERS; ++i)
        for (unsigned j=0; j<BENCH_PAYLOAD_WORDS; ++j)
            if (g_sub[i]->payload[j] != (uint32_t)g_sequence+j)
                fatal("payload word mismatch",i);
    vTaskDelay(pdMS_TO_TICKS(BENCH_PAUSE_MS));
}
static void report_stack_margin(void)
{
    for(unsigned i=0;i<=BENCH_SUBSCRIBERS;i++) {
        uint32_t app_words=policy_stack_watermark((TaskHandle_t)worker_tcb(i));
        if(app_words<64U) fatal("application stack margin",i);
        uint32_t syscall_words=0;
#if BENCH_MPU_ENABLED
        xMPU_SETTINGS *settings=xTaskGetMPUSettings((TaskHandle_t)worker_tcb(i));
        uint32_t *buffer=settings->xSystemCallStackInfo.ulSystemCallStackBuffer;
        while(syscall_words<configSYSTEM_CALL_STACK_SIZE && buffer[syscall_words]==0xa5a5a5a5U) ++syscall_words;
        if(syscall_words<64U) fatal("system call stack margin",i);
#endif
        SEGGER_RTT_printf(0,"[STACK] app=%u unused_app_bytes=%lu unused_syscall_bytes=%lu PASS\n",
            i,(unsigned long)(app_words*4U),(unsigned long)(syscall_words*4U));
    }
}
static void micro_ros_task(void *argument)
{
    (void)argument;
    g_controller=xTaskGetCurrentTaskHandle();
    SEGGER_RTT_printf(0,"[TRIM] mask=%u mpu_only=1 reg_reuse=1 current_mpu_inline=1\n",BENCH_TRIM_MASK);
#ifdef __OPTIMIZE__
    SEGGER_RTT_WriteString(0, "[BUILD] variant=app_sfi compiler_optimized=1\n[FIX] trusted_middleware_clock=1 extended_fault_frame=1\n");
#else
    SEGGER_RTT_WriteString(0, "[BUILD] variant=app_sfi compiler_optimized=0\n");
#endif
    SEGGER_RTT_printf(0,"[TUNE] stage=%u frame26=%u svc_O3=%u mair_equal_skip=%u active_lookup=%u\n",BENCH_TUNE_STAGE,1U,0U,0U,1U);
    SEGGER_RTT_printf(0,"[FLOW] requests_per_iteration=%u sub_receive_on_wake=1 pub_publish_returns=1 callback_mpu_unprivileged=%u\n",BENCH_SUBSCRIBERS+2U,BENCH_MPU_ENABLED);
    if (((MPU->CTRL & MPU_CTRL_ENABLE_Msk) != 0U) != (BENCH_MPU_ENABLED != 0))
        fatal("MPU state does not match mode", (int32_t)MPU->CTRL);
    uint32_t start = cycles();
    if (!bench_allocator_init()) fatal("allocator init", 0);
    g_init_cycles = cycles() - start;
    SEGGER_RTT_printf(0, "[OPT2] fast_paths=%d (0=previous, 1=optimized)\n", BENCH_FAST_PATHS);
    SEGGER_RTT_printf(0,"[WORKLOAD] direct_read_each_iteration=%u same_apps=1\n",BENCH_DIRECT_READ);
    allocator_selftest();
#if BENCH_SFI_ENABLED
    SEGGER_RTT_WriteString(0,"[SFI SCOPE] closed_C_apps=1 object_access_hooks=1 stack_static_validation=1 active_frame_write_denied=1 trusted_middleware=1 MPU=0\n");
#endif
    if (bench_policy_mode != BENCH_MODE) fatal("worker mode mismatch", (int32_t)bench_policy_mode);
    SEGGER_RTT_WriteString(0,"[DOMAIN] actual_pub_sub_apps; own_object+own_stack; trusted_middleware_syscall; C_apps\n");
    SEGGER_RTT_printf(0,"[FAST3] enabled=%d kernel_O3=%d trusted_allocator_mutex=%d systick_fix=1\n",BENCH_OPT3,BENCH_OPT3,BENCH_OPT3);
    SEGGER_RTT_printf(0,"[FAST3 WORKER] trusted_dispatch=%d static_lifetime=1\n",BENCH_OPT3 && BENCH_MPU_ENABLED);
    start = cycles();
    g_pub = microros_zero_allocate(1, BENCH_OBJECT_BYTES, NULL);
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i)
        g_sub[i] = microros_zero_allocate(1, BENCH_OBJECT_BYTES, NULL);
    g_layout_cycles = cycles() - start;
    if (!g_pub) fatal("PUB allocation", 0);
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i)
        if (!g_sub[i]) fatal("SUB allocation", (int32_t)i);
    /* Validate live allocations once before creating workers. Their addresses,
     * sizes and lifetime are immutable until reset. No freeing/reallocation. */
    guard(g_pub, BENCH_OBJECT_BYTES, false);
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i) {
        guard(g_sub[i], BENCH_OBJECT_BYTES, false);
        uintptr_t a = (uintptr_t)g_pub, b = (uintptr_t)g_sub[i];
        if (!(a + BENCH_OBJECT_BYTES <= b || b + BENCH_OBJECT_BYTES <= a))
            fatal("PUB/SUB overlap", (int32_t)i);
        for (uint32_t j = 0; j < i; ++j) {
            uintptr_t c = (uintptr_t)g_sub[j];
            if (!(c + BENCH_OBJECT_BYTES <= b || b + BENCH_OBJECT_BYTES <= c))
                fatal("SUB/SUB overlap", (int32_t)i);
        }
    }
    for(unsigned i=0;i<BENCH_SUBSCRIBERS;i++)
        if((uintptr_t)g_sub[i]!=(uintptr_t)g_pub+BENCH_OBJECT_BYTES*(i+1)) fatal("non-contiguous LowFat slots",i);
    SEGGER_RTT_WriteString(0,"[LAYOUT] adjacent_128_byte_slots=PASS\n");
    g_pub_wire.data.data = g_pub->payload;
    g_pub_wire.data.size = g_pub_wire.data.capacity = BENCH_PAYLOAD_WORDS;
    for (unsigned i=0; i<BENCH_SUBSCRIBERS; ++i) {
        g_sub_wire[i].data.data = g_sub[i]->payload;
        g_sub_wire[i].data.capacity = BENCH_PAYLOAD_WORDS;
    }
    SEGGER_RTT_printf(0,"[PAYLOAD] bytes=%u check_all=%u slot_bytes=%u type=UInt32MultiArray\n",
        BENCH_PAYLOAD_BYTES, BENCH_CHECK_ALL_WORDS, BENCH_OBJECT_BYTES);
    create_workers();
    SEGGER_RTT_printf(0, "[CORE] repeated_bounds=%d synthetic_alloc=0 periodic_read_in_timing=%u diagnostic_probes_outside_timing=1\n", BENCH_VALIDATE_EACH_MESSAGE,BENCH_DIRECT_READ);
    SEGGER_RTT_printf(0, "\n[COMPARE] mode=%d LowFat=%d MPU=%d SFI=%d subscribers=%d CPU_Hz=%lu warmup=%u samples=%u\n",
        BENCH_MODE, BENCH_LOWFAT_ENABLED, BENCH_MPU_ENABLED, BENCH_SFI_ENABLED, BENCH_SUBSCRIBERS,
        (unsigned long)SystemCoreClock, BENCH_WARMUP, BENCH_SAMPLES);
    SEGGER_RTT_printf(0, "PUB=%p object_bytes=%u allocator_pool_bytes=%u\n", (void *)g_pub,
        BENCH_OBJECT_BYTES, BENCH_LOWFAT_ENABLED ? LOWFAT_NUM_REGIONS * LOWFAT_REGION_SIZE : 65536U);
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i)
        SEGGER_RTT_printf(0, "SUB%u=%p data=%p\n", i + 1U, (void *)g_sub[i], (void *)&g_sub[i]->payload[0]);
    SEGGER_RTT_printf(0, "allocator_init_cycles=%lu object_alloc_cycles=%lu timer_pair_cycles=%lu\n",
        (unsigned long)g_init_cycles, (unsigned long)g_layout_cycles, (unsigned long)g_timer_cycles);
    bench_allocator_stats_t before_ros = bench_allocator_stats();
    rmw_uros_set_custom_transport(true, &hlpuart1, cubemx_transport_open,
        cubemx_transport_close, cubemx_transport_write, cubemx_transport_read);
    rcl_allocator_t allocator = rcl_get_default_allocator();
    allocator.allocate = microros_allocate;
    allocator.deallocate = microros_deallocate;
    allocator.reallocate = microros_reallocate;
    allocator.zero_allocate = microros_zero_allocate;
    if (!rcutils_set_default_allocator(&allocator)) fatal("allocator setup", 0);
    SEGGER_RTT_WriteString(0, "[micro-ROS] Waiting for agent...\n");
    while (rmw_uros_ping_agent(1000, 1) != RMW_RET_OK) vTaskDelay(pdMS_TO_TICKS(1000));
    CHECK_RCL(rclc_support_init(&g_support, 0, NULL, &allocator), "support");
    g_pub_node = rcl_get_zero_initialized_node();
    g_sub_node = rcl_get_zero_initialized_node();
    g_publisher = rcl_get_zero_initialized_publisher();
    
    CHECK_RCL(rclc_node_init_default(&g_pub_node, "node_a", "", &g_support), "Node A");
    CHECK_RCL(rclc_node_init_default(&g_sub_node, "node_b", "", &g_support), "Node B");
    CHECK_RCL(rclc_publisher_init_default(&g_publisher, &g_pub_node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, UInt32MultiArray), "bench_uint32_payload"), "publisher");

    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i) {
        g_subscriptions[i] = rcl_get_zero_initialized_subscription();
        CHECK_RCL(rclc_subscription_init_default(&g_subscriptions[i], &g_sub_node,
            ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, UInt32MultiArray), "bench_uint32_payload"), "subscription");
        g_executors[i]=rclc_executor_get_zero_initialized_executor();
        CHECK_RCL(rclc_executor_init(&g_executors[i],&g_support.context,1,&allocator),"executor");
        CHECK_RCL(rclc_executor_add_subscription(&g_executors[i], &g_subscriptions[i],
            &g_sub_wire[i], transport_callback, ON_NEW_DATA), "add subscription");
    }
    bench_allocator_stats_t after_ros = bench_allocator_stats();
    g_ros_alloc_calls = after_ros.allocate_calls + after_ros.calloc_calls + after_ros.realloc_calls
        - before_ros.allocate_calls - before_ros.calloc_calls - before_ros.realloc_calls;
    SEGGER_RTT_printf(0, "ROS_setup_allocator_calls=%lu max_request=%lu failures=%lu\n",
        (unsigned long)g_ros_alloc_calls, (unsigned long)after_ros.max_request, (unsigned long)after_ros.failures);
    if (!g_ros_alloc_calls) fatal("ROS allocator callbacks unused", 0);
    vTaskDelay(pdMS_TO_TICKS(BENCH_DISCOVERY_MS));
    SEGGER_RTT_WriteString(0, "[BENCH] Running warmup + samples; no per-message RTT output.\n");
    for (uint32_t i = 0; i < BENCH_WARMUP; ++i) run_one(false);
    uint32_t warmup_errors = g_publish_errors + g_timeouts + g_worker_errors + g_spin_errors + g_unexpected_rx;
    if (warmup_errors || g_seen != ALL_SUBS_MASK) fatal("communication before isolation", (int32_t)warmup_errors);
    SEGGER_RTT_WriteString(0,"[WARMUP DONE] normal_exchange=PASS\n");
    isolation_test("PRE");
    policy_test();
    g_publish_errors = g_timeouts = g_worker_errors = g_spin_errors = g_unexpected_rx = 0;
    bench_allocator_stats_t before_samples = bench_allocator_stats();
    for (uint32_t i = 0; i < BENCH_SAMPLES; ++i) run_one(true);
    bench_allocator_stats_t after_samples = bench_allocator_stats();
    SEGGER_RTT_printf(0,"[MEASURED ALLOCATOR] malloc=%lu calloc=%lu realloc=%lu free=%lu\n",
        (unsigned long)(after_samples.allocate_calls-before_samples.allocate_calls),
        (unsigned long)(after_samples.calloc_calls-before_samples.calloc_calls),
        (unsigned long)(after_samples.realloc_calls-before_samples.realloc_calls),
        (unsigned long)(after_samples.free_calls-before_samples.free_calls));
    SEGGER_RTT_WriteString(0, "\nCSV,mode,subscribers,metric,count,min_cycles,mean_cycles,max_cycles,mean_us,total_cycles,total_us\n");

    print_stat("iteration_total", &g_iteration_stats);
    print_stat("pub_dispatch_excluding_middleware",&g_dispatch_stats[0]);
    print_stat("sub_dispatch_excluding_middleware",&g_dispatch_sub_stats);
    print_stat("all_dispatch_excluding_middleware",&g_dispatch_all_stats);
    print_stat("receive_internal_total",&g_receive_internal_stats);
    print_stat("middleware_internal_total",&g_middleware_total_stats);
    const char *dispatch_names[]={"sub1_dispatch_excluding_middleware","sub2_dispatch_excluding_middleware","sub3_dispatch_excluding_middleware","sub4_dispatch_excluding_middleware"};
    for(unsigned i=0;i<BENCH_SUBSCRIBERS;i++) print_stat(dispatch_names[i],&g_dispatch_stats[i+1]);
    print_stat("app_execution_total_excluding_middleware", &g_app_total_stats);
    print_stat("pub_app_excluding_middleware", &g_app_stats[0]);
    const char *app_names[]={"sub1_app_excluding_middleware","sub2_app_excluding_middleware","sub3_app_excluding_middleware","sub4_app_excluding_middleware"};
    for(unsigned i=0;i<BENCH_SUBSCRIBERS;i++) print_stat(app_names[i],&g_app_stats[i+1]);
    print_stat("publisher_task_roundtrip", &g_worker_stats);
    print_stat("publish_call", &g_publish_stats);
    print_stat("subscriber_tasks_total", &g_sub_batch_stats);
    print_stat("pub_request_to_all_done", &g_e2e_stats);
    const char *names[] = {"sub1_task", "sub2_task", "sub3_task", "sub4_task"};
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i) {
        print_stat(names[i], &g_sub_worker_stats[i]);
        SEGGER_RTT_printf(0, "SUB%u received=%lu/%u\n", i + 1U, (unsigned long)g_sub_received[i], BENCH_SAMPLES);
    }
    SEGGER_RTT_printf(0, "warmup_errors=%lu publish_errors=%lu rx_timeouts=%lu worker_errors=%lu spin_errors=%lu unexpected_rx=%lu\n",
        (unsigned long)warmup_errors, (unsigned long)g_publish_errors, (unsigned long)g_timeouts,
        (unsigned long)g_worker_errors, (unsigned long)g_spin_errors, (unsigned long)g_unexpected_rx);
    bench_allocator_stats_t total = bench_allocator_stats();
    SEGGER_RTT_printf(0, "allocator malloc=%lu calloc=%lu realloc=%lu free=%lu failures=%lu invalid_frees=%lu max_request=%lu\n",
        (unsigned long)total.allocate_calls, (unsigned long)total.calloc_calls, (unsigned long)total.realloc_calls,
        (unsigned long)total.free_calls, (unsigned long)total.failures, (unsigned long)total.invalid_frees,
        (unsigned long)total.max_request);
    isolation_test("POST");
    /* A real normal exchange after the last fault must still work. */
    run_one(false);
    SEGGER_RTT_printf(0, "[RECOVERY] normal_exchange=%s\n",
        (g_received && g_seen == ALL_SUBS_MASK && !g_worker_errors && !g_timeouts && !g_publish_errors && !g_spin_errors && !g_unexpected_rx) ? "PASS" : "FAIL");
    report_stack_margin();
    uint32_t rounds=BENCH_WARMUP+BENCH_SAMPLES+1U;
    bool flow_ok=g_gate_pub_calls==rounds && g_gate_sub_calls==rounds*BENCH_SUBSCRIBERS;
    SEGGER_RTT_printf(0,"[APP FLOW] publish_calls=%lu receive_calls=%lu expected_rounds=%lu same_task_gate=%s\n",
        (unsigned long)g_gate_pub_calls,(unsigned long)g_gate_sub_calls,(unsigned long)rounds,flow_ok?"PASS":"FAIL");
    uint32_t expected_calls=BENCH_SAMPLES*(BENCH_SUBSCRIBERS+1U);
    bool calls_ok=g_measured_io_calls==expected_calls && g_measured_done_wait_calls==expected_calls &&
        g_measured_gate_entries==BENCH_SAMPLES*(BENCH_SUBSCRIBERS+2U);
    SEGGER_RTT_printf(0,"[SERVICE COUNT] measured_io=%lu measured_done=%lu expected_each=%lu %s\n",
        (unsigned long)g_measured_io_calls,(unsigned long)g_measured_done_wait_calls,(unsigned long)expected_calls,calls_ok?"PASS":"FAIL");
    SEGGER_RTT_printf(0,"[GATE COUNT] measured_entries=%lu total=%lu per_iteration=%u bootstrap_excluded=1 %s\n",
        (unsigned long)g_measured_gate_entries,(unsigned long)g_measured_gate_entries,BENCH_SUBSCRIBERS+2U,calls_ok?"PASS":"FAIL");
    bool direct_ok=g_direct_total==rounds*BENCH_DIRECT_READ && g_direct_measured==BENCH_SAMPLES*BENCH_DIRECT_READ &&
        g_direct_blocked==((BENCH_MPU_ENABLED || BENCH_SFI_ENABLED)?BENCH_SAMPLES*BENCH_DIRECT_READ:0U) && !g_direct_pending;
    SEGGER_RTT_printf(0,"[DIRECT READ] measured=%lu blocked=%lu total_with_warmup_recovery=%lu actual_rx_buffer=1 in_timing=1 %s\n",
        (unsigned long)g_direct_measured,(unsigned long)g_direct_blocked,
        (unsigned long)g_direct_total,direct_ok?"PASS":"FAIL");
    for(unsigned i=0;i<BENCH_SUBSCRIBERS;i++)
        SEGGER_RTT_printf(0,"[DIRECT TARGET] SUB%u measured=%lu\n",i+1,(unsigned long)g_direct_targets[i]);
#if BENCH_SFI_ENABLED
    SEGGER_RTT_printf(0,"[SFI COUNTS] measured_access_checks=%lu denied=%lu checks_in_timing=1\n",
        (unsigned long)g_sfi_checks,(unsigned long)g_sfi_denials);
    direct_ok=direct_ok && g_sfi_denials==BENCH_SAMPLES*BENCH_DIRECT_READ && g_sfi_checks>BENCH_SAMPLES;
#endif
    bool valid = direct_ok && calls_ok && flow_ok && g_policy_passes == BENCH_SUBSCRIBERS+1U && g_probe_passes == 2U * BENCH_SUBSCRIBERS && !warmup_errors && !total.failures && !total.invalid_frees &&
        g_iteration_stats.n == BENCH_SAMPLES &&
        g_bounds_stats.n == (BENCH_LOWFAT_ENABLED && BENCH_VALIDATE_EACH_MESSAGE ? BENCH_SAMPLES * (BENCH_SUBSCRIBERS + 1U) : 0U) &&
        !g_publish_errors && !g_timeouts && !g_worker_errors && !g_spin_errors &&
        !g_unexpected_rx && g_e2e_stats.n == BENCH_SAMPLES;
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i)
        valid = valid && g_sub_received[i] == BENCH_SAMPLES && g_sub_worker_stats[i].n == BENCH_SAMPLES;
    SEGGER_RTT_printf(0, "[BENCH DONE] mode=%d subscribers=%d valid=%s (reset to repeat)\n", BENCH_MODE, BENCH_SUBSCRIBERS, valid ? "YES" : "NO");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
void MPU_Demo_Start(void)
{
    SEGGER_RTT_Init();
    SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);
#if !BENCH_MPU_ENABLED
    __DMB(); MPU->CTRL = 0U; __DSB(); __ISB();
#endif
    clock_init();
    if (xTaskCreate(micro_ros_task, "microROS", MICROROS_STACK_WORDS, NULL,
        (tskIDLE_PRIORITY + 3U) | portPRIVILEGE_BIT, NULL) != pdPASS) fatal("microROS task creation", 0);
}
static void create_workers(void)
{
#if BENCH_LOWFAT_ENABLED
    if (((uintptr_t)g_pub % 32U) != 0U) fatal("PUB MPU alignment", 0);
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i)
        if (((uintptr_t)g_sub[i] % 32U) != 0U) fatal("SUB MPU alignment", (int32_t)i);
#endif
#if BENCH_MPU_ENABLED
    const TaskParameters_t parameters = {
        .pvTaskCode = publisher_app_task, .pcName = "PublisherApp",
        .usStackDepth = WORKER_STACK_WORDS, .pvParameters = g_pub,
        .uxPriority = tskIDLE_PRIORITY + 4U, .puxStackBuffer = g_worker_stack,
        .xRegions = {{ .pvBaseAddress = g_pub, .ulLengthInBytes = BENCH_OBJECT_BYTES,
            .ulParameters = tskMPU_REGION_READ_WRITE | tskMPU_REGION_EXECUTE_NEVER }, {0}, {0}},
        .pxTaskBuffer = &g_worker_tcb
    };
#if BENCH_OPT3
    if (!bench_fast_create(0U, &parameters)) fatal("worker task creation", 0);
#else
    xTaskCreateRestrictedStatic(&parameters, &g_worker);
#endif
#else
    g_worker = xTaskCreateStatic(publisher_app_task, "PublisherApp", WORKER_STACK_WORDS,
        g_pub, tskIDLE_PRIORITY + 4U, g_worker_stack, &g_worker_tcb);
#endif
#if !(BENCH_OPT3 && BENCH_MPU_ENABLED)
    if (!g_worker) fatal("worker task creation", 0);
#endif
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i) {
#if BENCH_MPU_ENABLED
        const TaskParameters_t sub_parameters = {
            .pvTaskCode = subscriber_app_task, .pcName = "SubscriberApp",
            .usStackDepth = WORKER_STACK_WORDS, .pvParameters = g_sub[i],
            .uxPriority = tskIDLE_PRIORITY + 4U, .puxStackBuffer = g_sub_stacks[i],
            .xRegions = {{ .pvBaseAddress = g_sub[i], .ulLengthInBytes = BENCH_OBJECT_BYTES,
                .ulParameters = tskMPU_REGION_READ_WRITE | tskMPU_REGION_EXECUTE_NEVER }, {0}, {0}},
            .pxTaskBuffer = &g_sub_tcbs[i]
        };
#if BENCH_OPT3
        if (!bench_fast_create(i+1U, &sub_parameters)) fatal("SUB worker creation", (int32_t)i);
#else
        if (xTaskCreateRestrictedStatic(&sub_parameters, &g_sub_workers[i]) != pdPASS)
            fatal("SUB worker creation", (int32_t)i);
#endif
#else
        g_sub_workers[i] = xTaskCreateStatic(subscriber_app_task, "SubscriberApp", WORKER_STACK_WORDS,
            g_sub[i], tskIDLE_PRIORITY + 4U, g_sub_stacks[i], &g_sub_tcbs[i]);
#endif
#if !(BENCH_OPT3 && BENCH_MPU_ENABLED)
        if (!g_sub_workers[i]) fatal("SUB worker creation", (int32_t)i);
#endif
    }

}
/* Recover ONLY the armed PUB read probe. All other faults invalidate the run. */
void BenchmarkFault(uint32_t *frame, uint32_t exc_return) __attribute__((used, noinline));
void BenchmarkFault(uint32_t *frame, uint32_t exc_return)
{
    /* Cortex-M core frame is r0-r3,r12,lr,pc,xPSR at SP (also when FP
     * extension follows it). Validate the complete basic/extended frame before recovery. */
#if BENCH_MPU_ENABLED
    extern TaskHandle_t pxCurrentTCB;
    uintptr_t sp = (uintptr_t)frame;
    uintptr_t lo = g_probe.stack_begin;
    uintptr_t hi = g_probe.stack_end;
    bool recover = false;
    if (isolation_stack_valid(sp, lo, hi)) {
        const isolation_fault_info_t f = {
            .sp=sp, .stack_begin=lo, .stack_end=hi,
            .task=(uintptr_t)pxCurrentTCB, .publisher_task=g_probe.task,
            .exception=__get_IPSR(), .exc_return=exc_return,
            .armed=g_probe.armed, .blocked=g_probe.blocked,
            .cfsr=SCB->CFSR, .hfsr=SCB->HFSR, .fault_address=SCB->MMFAR,
            .target=g_probe.target, .pc=frame[6], .load_pc=g_probe.expected_pc,
            .address_register=frame[0], .control=frame[3], .xpsr=frame[7]
        };
        recover = isolation_fault_matches(&f);
    }
    if (recover) {
        g_probe.blocked = 1;
        g_probe.fault_pc = frame[6];
        g_probe.cfsr = SCB->CFSR;
        g_probe.control = frame[3];
        g_probe.armed = 0;
        frame[0] = DOMAIN_BLOCKED;
        /* Return the diagnostic sentinel to the calling C application. */
        frame[6] = g_probe.recovery_pc & ~1U;
        SCB->CFSR = 0x82U; /* W1C DACCVIOL + MMARVALID; never clear unrelated faults. */
        __DSB(); __ISB();
        return;
    }
#else
    (void)exc_return;
#endif
    SEGGER_RTT_printf(0, "\n[BENCH INVALID: FAULT] PC=0x%08lx CFSR=0x%08lx HFSR=0x%08lx MMFAR=0x%08lx\n",
        (unsigned long)frame[6], (unsigned long)SCB->CFSR,
        (unsigned long)SCB->HFSR, (unsigned long)SCB->MMFAR);
    SEGGER_RTT_printf(0, "[FAULT CONTEXT] LR=0x%08lx xPSR=0x%08lx EXC_RETURN=0x%08lx ICSR=0x%08lx\n",
        (unsigned long)frame[5], (unsigned long)frame[7],
        (unsigned long)exc_return, (unsigned long)SCB->ICSR);
    for (;;) { __NOP(); }
}
__attribute__((naked)) void MemManage_Handler(void)
{
    __asm volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mov r1, lr\n b BenchmarkFault\n");
}
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mov r1, lr\n b BenchmarkFault\n");
}
void vAssertCalled(const char *file, int line) { taskDISABLE_INTERRUPTS(); fatal(file, line); }
void vApplicationStackOverflowHook(TaskHandle_t task, char *name) { (void)task; fatal(name, -1); }





