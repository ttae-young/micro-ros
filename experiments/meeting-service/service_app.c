/* STM32L552 adaptation of the micro-ROS FreeRTOS AddTwoInts demo.
 * https://github.com/micro-ROS/freertos_apps/blob/7a426032bd3c33d36d3a6411affc6881522bc05b/apps/add_two_ints_service/app.c
 * SPDX-License-Identifier: Apache-2.0
 */
#include "main.h"
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "FreeRTOS.h"
#include "task.h"
#include "SEGGER_RTT.h"
#include "bench_allocator.h"
#include "benchmark_config.h"
#include "freertos_mpu_demo.h"
#include <example_interfaces/srv/add_two_ints.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rmw_microros/rmw_microros.h>
#include <uxr/client/transport.h>

#if BENCH_MODE != 1
#error "The service bring-up uses baseline mode."
#endif

extern UART_HandleTypeDef hlpuart1;
bool cubemx_transport_open(struct uxrCustomTransport *);
bool cubemx_transport_close(struct uxrCustomTransport *);
size_t cubemx_transport_write(struct uxrCustomTransport *, const uint8_t *, size_t, uint8_t *);
size_t cubemx_transport_read(struct uxrCustomTransport *, uint8_t *, size_t, int, uint8_t *);

static bool request_seen;

static void fail(const char *stage, int code)
{
    SEGGER_RTT_printf(0, "[BENCH ERROR] AddTwoInts %s code=%d\n", stage, code);
    for (;;) {}
}

void vAssertCalled(const char *file, int line)
{
    taskDISABLE_INTERRUPTS();
    fail(file, line);
}

static void check(const char *stage, rcl_ret_t status)
{
    if (status != RCL_RET_OK) fail(stage, status);
}

static void add_two_ints(const void *request, void *response)
{
    const example_interfaces__srv__AddTwoInts_Request *input = request;
    example_interfaces__srv__AddTwoInts_Response *output = response;
    if (__builtin_add_overflow(input->a, input->b, &output->sum)) {
        output->sum = input->a >= 0 ? INT64_MAX : INT64_MIN;
    }
    request_seen = true;
}

static void service_task(void *argument)
{
    (void)argument;
    if (!bench_allocator_init()) fail("LowFat init", 0);
    rmw_ret_t transport_status = rmw_uros_set_custom_transport(true, &hlpuart1, cubemx_transport_open,
        cubemx_transport_close, cubemx_transport_write, cubemx_transport_read);
    if (transport_status != RMW_RET_OK) fail("UART transport", transport_status);
    rcl_allocator_t allocator = rcl_get_default_allocator();
    allocator.allocate = microros_allocate;
    allocator.deallocate = microros_deallocate;
    allocator.reallocate = microros_reallocate;
    allocator.zero_allocate = microros_zero_allocate;
    if (!rcutils_set_default_allocator(&allocator)) fail("allocator", 0);

    SEGGER_RTT_WriteString(0, "[SERVICE] Waiting for Agent\n");
    while (rmw_uros_ping_agent(1000, 1) != RMW_RET_OK) vTaskDelay(pdMS_TO_TICKS(1000));

    rclc_support_t support = {0};
    rcl_node_t node = rcl_get_zero_initialized_node();
    rcl_service_t service = rcl_get_zero_initialized_service();
    rclc_executor_t executor = rclc_executor_get_zero_initialized_executor();
    example_interfaces__srv__AddTwoInts_Request request = {0};
    example_interfaces__srv__AddTwoInts_Response response = {0};
    check("support", rclc_support_init(&support, 0, NULL, &allocator));
    check("node", rclc_node_init_default(&node, "meeting_add_twoints", "", &support));
    check("service", rclc_service_init_default(&service, &node,
        ROSIDL_GET_SRV_TYPE_SUPPORT(example_interfaces, srv, AddTwoInts), "/addtwoints"));
    check("executor", rclc_executor_init(&executor, &support.context, 1, &allocator));
    check("add service", rclc_executor_add_service(&executor, &service, &request, &response, add_two_ints));
    SEGGER_RTT_WriteString(0, "[SERVICE READY] /addtwoints\n");

    bool reported = false;
    for (;;) {
        rcl_ret_t status = rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10));
        if (status != RCL_RET_TIMEOUT) check("spin", status);
        if (request_seen && !reported) {
            reported = true;
            SEGGER_RTT_WriteString(0, "[BENCH DONE] workload=add_two_ints valid=YES\n");
        }
        vTaskDelay(1);
    }
}

void MPU_Demo_Start(void)
{
    SEGGER_RTT_Init();
    SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);
    __DMB();
    MPU->CTRL = 0U;
    __DSB();
    __ISB();
    if (xTaskCreate(service_task, "AddTwoInts", 4096, NULL,
        (tskIDLE_PRIORITY + 3U) | portPRIVILEGE_BIT, NULL) != pdPASS) fail("task", 0);
}
