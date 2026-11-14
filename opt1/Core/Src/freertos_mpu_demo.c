/* Core experiment: normal micro-ROS communication and actual unprivileged
 * PUB -> SUB reads. Expected faults are tested OUTSIDE measured iterations.
 * Objects/tasks have static lifetime; no message batching or privilege widening. */
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "SEGGER_RTT.h"
#include "freertos_mpu_demo.h"
#include "benchmark_config.h"
#include "bench_allocator.h"
#include "lowfat_malloc.h"
#include "bench_fast_worker.h"
#include "isolation_fault.h"
#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>
#include <rcutils/allocator.h>
#include <std_msgs/msg/int32.h>
#include <uxr/client/transport.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>



#define WORKER_STACK_WORDS 256U
#define MICROROS_STACK_WORDS 4096U
typedef struct {
    rcl_node_t node;
    rcl_publisher_t publisher;
    std_msgs__msg__Int32 message;
    volatile int32_t worker_value;
} publisher_region_t;
typedef struct {
    rcl_subscription_t subscription;
    std_msgs__msg__Int32 message;
    volatile int32_t worker_value;
} subscriber_region_t;
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

_Static_assert(sizeof(publisher_region_t) <= BENCH_OBJECT_BYTES, "PUB exceeds MPU slot");
_Static_assert(sizeof(subscriber_region_t) <= BENCH_OBJECT_BYTES, "SUB exceeds MPU slot");
static statistics_t g_bounds_stats, g_iteration_stats;
static uint32_t g_init_cycles, g_ros_alloc_calls;
static void create_workers(void);
static publisher_region_t *g_pub;
static subscriber_region_t *g_sub[BENCH_SUBSCRIBERS];
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
static rclc_executor_t g_executor;
static statistics_t g_publish_stats, g_worker_stats, g_e2e_stats;
static statistics_t g_sub_worker_stats[BENCH_SUBSCRIBERS], g_sub_batch_stats;
static uint32_t g_seen, g_batch_cycles, g_sub_received[BENCH_SUBSCRIBERS];
static bool g_measured;
#define ALL_SUBS_MASK ((1U << BENCH_SUBSCRIBERS) - 1U)
static uint32_t g_layout_cycles, g_timer_cycles;
static uint32_t g_publish_errors, g_timeouts, g_unexpected_rx, g_worker_errors, g_spin_errors;
static int32_t g_sequence;
static bool g_waiting, g_received;
static uint32_t g_rx_cycle;

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
static void print_stat(const char *name, const statistics_t *s)
{
    uint32_t mean = s->n ? (uint32_t)(s->sum / s->n) : 0U;
    uint64_t ns = ((uint64_t)mean * 1000000000ULL) / SystemCoreClock;
    SEGGER_RTT_printf(0, "CSV,%d,%d,%s,%lu,%lu,%lu,%lu,%lu.%03lu\n", BENCH_MODE, BENCH_SUBSCRIBERS, name,
        (unsigned long)s->n, (unsigned long)s->min, (unsigned long)mean,
        (unsigned long)s->max, (unsigned long)(ns / 1000U), (unsigned long)(ns % 1000U));
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
/* A probe command is used only outside normal traffic. Its address is passed
 * through PUB-owned memory; the worker cannot read the service's g_sub array.
 * The normal positive sequence numbers can never equal this command. */
#define PROBE_COMMAND INT32_MIN
#define PROBE_BLOCKED (INT32_MIN + 1)
typedef struct {
    volatile uint32_t armed, blocked, target, fault_pc, cfsr, control;
} probe_state_t;
static probe_state_t g_probe PRIVILEGED_DATA;
static uint32_t g_probe_passes;
extern const unsigned char isolation_load_site[], isolation_resume_site[];
/* r1 retains the exact target; r2 captures the caller privilege before fault.
 * A single, known load is the ONLY instruction that may be recovered. */
__attribute__((naked, noinline, used)) static int32_t isolation_read(const void *address __attribute__((unused)))
{
    __asm volatile(
        "mov r1, r0\n"
        "mrs r2, control\n"
        ".global isolation_load_site\n"
        "isolation_load_site: ldr r0, [r1]\n"
        ".global isolation_resume_site\n"
        "isolation_resume_site: bx lr\n");
}
static void publisher_worker_task(void *argument)
{
    publisher_region_t *pub = argument;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (pub->worker_value == PROBE_COMMAND)
            pub->worker_value = isolation_read((const void *)(uintptr_t)(uint32_t)pub->message.data);
        else
            pub->worker_value = pub->message.data;
    }
}
static void notify_publisher(void)
{
#if BENCH_OPT3 && BENCH_MPU_ENABLED
    if (!bench_fast_notify(0U)) fatal("PUB notify", 0);
#else
    xTaskNotifyGive(g_worker);
#endif
}
/* Called with the executor stopped, after actual messages have been received.
 * Targets every subscriber, before AND after timing; no probe in timing. */
static void isolation_test(const char *phase)
{
    int32_t saved = g_pub->message.data;
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i) {
        if (SCB->CFSR || SCB->HFSR) fatal("stale fault status", 0);
        int32_t expected = g_sub[i]->message.data;
        g_probe.target = (uint32_t)(uintptr_t)&g_sub[i]->message.data;
        g_probe.blocked = g_probe.fault_pc = g_probe.cfsr = g_probe.control = 0;
        g_pub->message.data = (int32_t)g_probe.target;
        g_pub->worker_value = PROBE_COMMAND;
        g_probe.armed = 1;
        __DMB();
        notify_publisher();
        __DMB();
        g_probe.armed = 0;
        bool ok = BENCH_MPU_ENABLED ?
            (g_probe.blocked == 1U && g_pub->worker_value == PROBE_BLOCKED) :
            (g_probe.blocked == 0U && g_pub->worker_value == expected);
        if (g_sub[i]->message.data != expected) ok = false;
        SEGGER_RTT_printf(0, "[ISOLATION %s] PUB -> SUB%u address=0x%08lx %s result=%s\n",
            phase, i+1U, (unsigned long)g_probe.target,
            g_probe.blocked ? "BLOCKED BY MPU" : "READ ALLOWED",
            ok ? "PASS" : "FAIL");
        if (!g_probe.blocked)
            SEGGER_RTT_printf(0, "  read_value=%ld SUB_message=%ld\n", (long)g_pub->worker_value, (long)expected);
        if (g_probe.blocked)
            SEGGER_RTT_printf(0, "  fault_PC=0x%08lx CFSR=0x%08lx caller_CONTROL=0x%08lx\n",
                (unsigned long)g_probe.fault_pc, (unsigned long)g_probe.cfsr, (unsigned long)g_probe.control);
        if (!ok) fatal("isolation test", (int32_t)i);
        ++g_probe_passes;
    }
    g_pub->message.data = saved;
    g_pub->worker_value = saved;
}
static void subscriber_worker_task(void *argument)
{
    subscriber_region_t *sub = argument;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        sub->worker_value = sub->message.data;
    }
}
static void subscription_callback(const void *message)
{
    const std_msgs__msg__Int32 *received = message;
    uint32_t i;
    for (i = 0; i < BENCH_SUBSCRIBERS; ++i)
        if (received == &g_sub[i]->message) break;
#if BENCH_VALIDATE_EACH_MESSAGE
    if (i < BENCH_SUBSCRIBERS) guard(g_sub[i], BENCH_OBJECT_BYTES, g_measured);
#endif
    if (i == BENCH_SUBSCRIBERS || !g_waiting || received->data != g_sequence || (g_seen & (1U << i))) {
        g_unexpected_rx++;
        return;
    }
    g_sub[i]->worker_value = -1;
    uint32_t start = cycles();
#if BENCH_OPT3 && BENCH_MPU_ENABLED
    if (!bench_fast_notify(i+1U)) fatal("SUB notify", (int32_t)i);
#else
    xTaskNotifyGive(g_sub_workers[i]);
#endif
    uint32_t elapsed = cycles() - start;
    if (g_sub[i]->worker_value != g_sequence) g_worker_errors++;
    if (g_measured) {
        add(&g_sub_worker_stats[i], elapsed);
        g_sub_received[i]++;
    }
    g_batch_cycles += elapsed;
    g_seen |= 1U << i;
    if (g_seen == ALL_SUBS_MASK) {
        g_rx_cycle = cycles();
        g_received = true;
    }
}
static void run_one(bool measured)
{
    uint32_t iteration_start = cycles();
#if BENCH_VALIDATE_EACH_MESSAGE
    guard(g_pub, BENCH_OBJECT_BYTES, measured);
#endif
    g_pub->message.data = ++g_sequence;
    g_pub->worker_value = -1;
    uint32_t start = cycles();
    notify_publisher();
    /* Higher priority worker finishes and blocks before this caller resumes. */
    uint32_t worker_cycles = cycles() - start;
    if (g_pub->worker_value != g_sequence) g_worker_errors++;
    g_received = false;
    g_seen = 0U;
    g_batch_cycles = 0U;
    g_measured = measured;
    g_waiting = true;
    TickType_t wait_start = xTaskGetTickCount();
    uint32_t publish_start = cycles();
    rcl_ret_t rc = rcl_publish(&g_pub->publisher, &g_pub->message, NULL);
    uint32_t publish_cycles = cycles() - publish_start;
    if (measured) {
        add(&g_worker_stats, worker_cycles);
        add(&g_publish_stats, publish_cycles);
    }
    if (rc != RCL_RET_OK) g_publish_errors++;
    else {
        while (!g_received && (TickType_t)(xTaskGetTickCount() - wait_start) < pdMS_TO_TICKS(BENCH_RX_TIMEOUT_MS)) {
            rc = rclc_executor_spin_some(&g_executor, RCL_MS_TO_NS(1));
            if (rc != RCL_RET_OK && rc != RCL_RET_TIMEOUT) { g_spin_errors++; break; }
        }
        /* Check after spin_some too: a blocking transport/library call can
         * return after the deadline. Do not count that as a valid sample. */
        if (g_received && (TickType_t)(xTaskGetTickCount() - wait_start) < pdMS_TO_TICKS(BENCH_RX_TIMEOUT_MS)) {
            if (measured) {
                add(&g_e2e_stats, g_rx_cycle - publish_start);
                add(&g_sub_batch_stats, g_batch_cycles);
            }
        } else g_timeouts++;
    }
    g_waiting = false;
    if (measured && g_received && (TickType_t)(xTaskGetTickCount() - wait_start) < pdMS_TO_TICKS(BENCH_RX_TIMEOUT_MS))
        add(&g_iteration_stats, cycles() - iteration_start);
    vTaskDelay(pdMS_TO_TICKS(BENCH_PAUSE_MS));
}
static void micro_ros_task(void *argument)
{
    (void)argument;
#ifdef __OPTIMIZE__
    SEGGER_RTT_WriteString(0, "[BUILD] variant=lowfat_isolation compiler_optimized=1\n");
#else
    SEGGER_RTT_WriteString(0, "[BUILD] variant=lowfat_isolation compiler_optimized=0\n");
#endif
    if (((MPU->CTRL & MPU_CTRL_ENABLE_Msk) != 0U) != (BENCH_MPU_ENABLED != 0))
        fatal("MPU state does not match mode", (int32_t)MPU->CTRL);
    uint32_t start = cycles();
    if (!bench_allocator_init()) fatal("allocator init", 0);
    g_init_cycles = cycles() - start;
    SEGGER_RTT_printf(0, "[OPT2] fast_paths=%d (0=previous, 1=optimized)\n", BENCH_FAST_PATHS);
    allocator_selftest();
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
    create_workers();
    SEGGER_RTT_printf(0, "[CORE] repeated_bounds=%d synthetic_alloc=0 probe_outside_timing=1\n", BENCH_VALIDATE_EACH_MESSAGE);
    SEGGER_RTT_printf(0, "\n[COMPARE] mode=%d LowFat=%d MPU=%d subscribers=%d CPU_Hz=%lu warmup=%u samples=%u\n",
        BENCH_MODE, BENCH_LOWFAT_ENABLED, BENCH_MPU_ENABLED, BENCH_SUBSCRIBERS,
        (unsigned long)SystemCoreClock, BENCH_WARMUP, BENCH_SAMPLES);
    SEGGER_RTT_printf(0, "PUB=%p object_bytes=%u allocator_pool_bytes=%u\n", (void *)g_pub,
        BENCH_OBJECT_BYTES, BENCH_LOWFAT_ENABLED ? LOWFAT_NUM_REGIONS * LOWFAT_REGION_SIZE : 65536U);
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i)
        SEGGER_RTT_printf(0, "SUB%u=%p data=%p\n", i + 1U, (void *)g_sub[i], (void *)&g_sub[i]->message.data);
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
    g_pub->node = rcl_get_zero_initialized_node();
    g_sub_node = rcl_get_zero_initialized_node();
    g_pub->publisher = rcl_get_zero_initialized_publisher();
    g_executor = rclc_executor_get_zero_initialized_executor();
    CHECK_RCL(rclc_node_init_default(&g_pub->node, "node_a", "", &g_support), "Node A");
    CHECK_RCL(rclc_node_init_default(&g_sub_node, "node_b", "", &g_support), "Node B");
    CHECK_RCL(rclc_publisher_init_default(&g_pub->publisher, &g_pub->node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "shared_int32_topic"), "publisher");
    CHECK_RCL(rclc_executor_init(&g_executor, &g_support.context, BENCH_SUBSCRIBERS, &allocator), "executor");
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i) {
        g_sub[i]->subscription = rcl_get_zero_initialized_subscription();
        CHECK_RCL(rclc_subscription_init_default(&g_sub[i]->subscription, &g_sub_node,
            ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32), "shared_int32_topic"), "subscription");
        CHECK_RCL(rclc_executor_add_subscription(&g_executor, &g_sub[i]->subscription,
            &g_sub[i]->message, subscription_callback, ON_NEW_DATA), "add subscription");
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
    isolation_test("PRE");
    g_publish_errors = g_timeouts = g_worker_errors = g_spin_errors = g_unexpected_rx = 0;
    for (uint32_t i = 0; i < BENCH_SAMPLES; ++i) run_one(true);
    SEGGER_RTT_WriteString(0, "\nCSV,mode,subscribers,metric,count,min_cycles,mean_cycles,max_cycles,mean_us\n");

    print_stat("iteration_total", &g_iteration_stats);
    print_stat("bounds_check", &g_bounds_stats);
    print_stat("worker_roundtrip", &g_worker_stats);
    print_stat("publish_call", &g_publish_stats);
    print_stat("sub_workers_total", &g_sub_batch_stats);
    print_stat("pub_to_all_done", &g_e2e_stats);
    const char *names[] = {"sub1_worker", "sub2_worker", "sub3_worker", "sub4_worker"};
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
    bool valid = g_probe_passes == 2U * BENCH_SUBSCRIBERS && !warmup_errors && !total.failures && !total.invalid_frees &&
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
#if BENCH_MPU_ENABLED
    if (((uintptr_t)g_pub % 32U) != 0U) fatal("PUB MPU alignment", 0);
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i)
        if (((uintptr_t)g_sub[i] % 32U) != 0U) fatal("SUB MPU alignment", (int32_t)i);
    const TaskParameters_t parameters = {
        .pvTaskCode = publisher_worker_task, .pcName = "PubWorker",
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
    g_worker = xTaskCreateStatic(publisher_worker_task, "PubWorker", WORKER_STACK_WORDS,
        g_pub, tskIDLE_PRIORITY + 4U, g_worker_stack, &g_worker_tcb);
#endif
#if !(BENCH_OPT3 && BENCH_MPU_ENABLED)
    if (!g_worker) fatal("worker task creation", 0);
#endif
    for (uint32_t i = 0; i < BENCH_SUBSCRIBERS; ++i) {
#if BENCH_MPU_ENABLED
        const TaskParameters_t sub_parameters = {
            .pvTaskCode = subscriber_worker_task, .pcName = "SubWorker",
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
        g_sub_workers[i] = xTaskCreateStatic(subscriber_worker_task, "SubWorker", WORKER_STACK_WORDS,
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
     * extension follows it). Only a basic frame is accepted for this integer probe. */
#if BENCH_MPU_ENABLED
    extern TaskHandle_t pxCurrentTCB;
    uintptr_t sp = (uintptr_t)frame;
    uintptr_t lo = (uintptr_t)g_worker_stack;
    uintptr_t hi = lo + sizeof(g_worker_stack);
    bool recover = false;
    if (isolation_stack_valid(sp, lo, hi)) {
        const isolation_fault_info_t f = {
            .sp=sp, .stack_begin=lo, .stack_end=hi,
            .task=(uintptr_t)pxCurrentTCB, .publisher_task=(uintptr_t)&g_worker_tcb,
            .exception=__get_IPSR(), .exc_return=exc_return,
            .armed=g_probe.armed, .blocked=g_probe.blocked,
            .cfsr=SCB->CFSR, .hfsr=SCB->HFSR, .fault_address=SCB->MMFAR,
            .target=g_probe.target, .pc=frame[6], .load_pc=(uintptr_t)isolation_load_site,
            .address_register=frame[1], .control=frame[2], .xpsr=frame[7]
        };
        recover = isolation_fault_matches(&f);
    }
    if (recover) {
        g_probe.blocked = 1;
        g_probe.fault_pc = frame[6];
        g_probe.cfsr = SCB->CFSR;
        g_probe.control = frame[2];
        g_probe.armed = 0;
        frame[0] = (uint32_t)PROBE_BLOCKED;
        frame[6] = (uintptr_t)isolation_resume_site & ~1U;
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

