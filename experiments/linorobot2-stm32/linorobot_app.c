/* Adapted from Linorobot2 Humble (077cfee58d6f7b9fd376857b104158c1b3b696e3).
 * Copyright (c) 2021 Juan Miguel Jimeno. SPDX-License-Identifier: Apache-2.0
 * Changes: C port of the two-wheel path, deterministic encoder input,
 * no motor output, and one Odometry response per Twist command.
 */
#include "main.h"
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "FreeRTOS.h"
#include "task.h"
#include "SEGGER_RTT.h"
#include "bench_allocator.h"
#include "benchmark_config.h"
#include "freertos_mpu_demo.h"
#include <geometry_msgs/msg/twist.h>
#include <nav_msgs/msg/odometry.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rmw_microros/rmw_microros.h>
#include <uxr/client/transport.h>
#include <math.h>

#if BENCH_MODE != 1
#error "This application profile is the baseline bring-up."
#endif

enum { WARMUP = 10, SAMPLES = 100, TOTAL_ROUNDS = WARMUP + SAMPLES };
static const float wheel_circumference = 0.065f * 3.14159265358979323846f;
static const float half_track = 0.095f;
static const float step_seconds = 0.02f;
static const float rpm_limit = 180.0f;
static const float encoder_rpm[][2] = {
    {0, 0}, {30, 30}, {42, 66}, {68, 40}, {70, 70}, {-32, -32}, {-30, 30}, {12, -18}
};

typedef struct {
    double integral;
    double previous_error;
} pid_state_t;

static rcl_publisher_t publisher;
static geometry_msgs__msg__Twist command;
static nav_msgs__msg__Odometry odom;
static char odom_frame[] = "odom";
static char body_frame[] = "base_footprint";
static pid_state_t left_pid, right_pid;
static float x, y, heading;
static volatile double left_pwm, right_pwm;
static uint32_t rounds, app_cycles, publish_cycles;

extern UART_HandleTypeDef hlpuart1;
bool cubemx_transport_open(struct uxrCustomTransport *);
bool cubemx_transport_close(struct uxrCustomTransport *);
size_t cubemx_transport_write(struct uxrCustomTransport *, const uint8_t *, size_t, uint8_t *);
size_t cubemx_transport_read(struct uxrCustomTransport *, uint8_t *, size_t, int, uint8_t *);

static void fail(const char *stage, int code)
{
    SEGGER_RTT_printf(0, "[BENCH ERROR] linorobot2 %s code=%d\n", stage, code);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

void vAssertCalled(const char *file, int line)
{
    taskDISABLE_INTERRUPTS();
    SEGGER_RTT_printf(0, "[BENCH ERROR] FreeRTOS assert %s:%d\n", file, line);
    for (;;) {}
}

static void check(const char *stage, rcl_ret_t status)
{
    if (status != RCL_RET_OK) fail(stage, status);
}

static double wheel_pid(pid_state_t *state, float target, float actual)
{
    double error = target - actual;
    double derivative = error - state->previous_error;
    state->integral += error;
    if (target == 0.0f && error == 0.0) {
        state->integral = 0.0;
        derivative = 0.0;
    }
    state->previous_error = error;
    double output = 0.6 * error + 0.08 * state->integral + 0.02 * derivative;
    return fmax(-255.0, fmin(255.0, output));
}

static void control_step(const geometry_msgs__msg__Twist *twist, uint32_t round)
{
    float forward_rpm = (float)twist->linear.x * 60.0f / wheel_circumference;
    float turn_rpm = (float)twist->angular.z * half_track * 60.0f / wheel_circumference;
    float peak = fabsf(forward_rpm) + fabsf(turn_rpm);
    if (peak > rpm_limit) {
        float scale = rpm_limit / peak;
        forward_rpm *= scale;
        turn_rpm *= scale;
    }

    const float *actual = encoder_rpm[(round - 1U) % (sizeof encoder_rpm / sizeof encoder_rpm[0])];
    left_pwm = wheel_pid(&left_pid, forward_rpm - turn_rpm, actual[0]);
    right_pwm = wheel_pid(&right_pid, forward_rpm + turn_rpm, actual[1]);

    float velocity = (actual[0] + actual[1]) * wheel_circumference / 120.0f;
    float yaw_rate = (actual[1] - actual[0]) * wheel_circumference / (120.0f * half_track);
    x += velocity * cosf(heading) * step_seconds;
    y += velocity * sinf(heading) * step_seconds;
    heading += yaw_rate * step_seconds;
    odom.pose.pose.position.x = x;
    odom.pose.pose.position.y = y;
    odom.pose.pose.orientation.z = sinf(heading * 0.5f);
    odom.pose.pose.orientation.w = cosf(heading * 0.5f);
    odom.twist.twist.linear.x = velocity;
    odom.twist.twist.angular.z = yaw_rate;
}

static void on_twist(const void *input)
{
    const geometry_msgs__msg__Twist *twist = input;
    if (!isfinite(twist->linear.x) || !isfinite(twist->angular.z) ||
        fabs(twist->linear.x) > 2.0 || fabs(twist->angular.z) > 10.0) {
        fail("invalid Twist", 0);
    }

    uint32_t round = ++rounds;
    uint32_t start = DWT->CYCCNT;
    control_step(twist, round);
    uint32_t control_cycles = DWT->CYCCNT - start;

    /* Logical replay time identifies the response for the one-command-at-a-time host. */
    odom.header.stamp.sec = (int32_t)(round / 50U);
    odom.header.stamp.nanosec = (round % 50U) * 20000000U;
    start = DWT->CYCCNT;
    rcl_ret_t status = rcl_publish(&publisher, &odom, NULL);
    uint32_t sent_cycles = DWT->CYCCNT - start;
    check("publish Odometry", status);

    if (round > WARMUP && round <= TOTAL_ROUNDS) {
        app_cycles += control_cycles;
        publish_cycles += sent_cycles;
    }
    if (round == TOTAL_ROUNDS) {
        SEGGER_RTT_printf(0, "[LINOROBOT RESULT] hz=%lu samples=%u control_total_cycles=%lu publish_total_cycles=%lu\n",
            (unsigned long)SystemCoreClock, (unsigned)SAMPLES,
            (unsigned long)app_cycles, (unsigned long)publish_cycles);
        SEGGER_RTT_printf(0, "[BENCH DONE] workload=linorobot2 mode=1 valid=YES\n");
    }
}

static void linorobot_task(void *arg)
{
    (void)arg;
    if (!bench_allocator_init()) fail("LowFat init", 0);
    rmw_ret_t transport_status = rmw_uros_set_custom_transport(true, &hlpuart1, cubemx_transport_open,
        cubemx_transport_close, cubemx_transport_write, cubemx_transport_read);
    if (transport_status != RMW_RET_OK) fail("UART transport", transport_status);
    rcl_allocator_t allocator = rcl_get_default_allocator();
    allocator.allocate = microros_allocate;
    allocator.deallocate = microros_deallocate;
    allocator.reallocate = microros_reallocate;
    allocator.zero_allocate = microros_zero_allocate;
    if (!rcutils_set_default_allocator(&allocator)) fail("install allocator", 0);

    SEGGER_RTT_WriteString(0, "[LINOROBOT] Waiting for Agent\n");
    while (rmw_uros_ping_agent(1000, 1) != RMW_RET_OK) vTaskDelay(pdMS_TO_TICKS(1000));

    rclc_support_t support = {0};
    rcl_node_t node = rcl_get_zero_initialized_node();
    rcl_subscription_t subscription = rcl_get_zero_initialized_subscription();
    rclc_executor_t executor = rclc_executor_get_zero_initialized_executor();
    publisher = rcl_get_zero_initialized_publisher();
    odom.header.frame_id = (rosidl_runtime_c__String){odom_frame, sizeof odom_frame - 1U, sizeof odom_frame};
    odom.child_frame_id = (rosidl_runtime_c__String){body_frame, sizeof body_frame - 1U, sizeof body_frame};
    odom.pose.covariance[0] = odom.pose.covariance[7] = odom.pose.covariance[35] = 0.001;
    odom.twist.covariance[0] = odom.twist.covariance[7] = odom.twist.covariance[35] = 0.0001;

    check("support", rclc_support_init(&support, 0, NULL, &allocator));
    check("node", rclc_node_init_default(&node, "linorobot2_bench", "", &support));
    check("publisher", rclc_publisher_init_default(&publisher, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(nav_msgs, msg, Odometry), "/linorobot2_bench/odom"));
    check("subscription", rclc_subscription_init_default(&subscription, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist), "/linorobot2_bench/cmd_vel"));
    check("executor", rclc_executor_init(&executor, &support.context, 1, &allocator));
    check("add subscription", rclc_executor_add_subscription(&executor, &subscription,
        &command, on_twist, ON_NEW_DATA));
    SEGGER_RTT_WriteString(0, "[LINOROBOT READY] baseline LowFat=1 MPU=0 SFI=0 warmup=10 samples=100\n");

    for (;;) {
        rcl_ret_t status = rclc_executor_spin_some(&executor, RCL_MS_TO_NS(10));
        if (status != RCL_RET_TIMEOUT) check("spin", status);
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
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if (xTaskCreate(linorobot_task, "Linorobot", 4096, NULL,
        (tskIDLE_PRIORITY + 3U) | portPRIVILEGE_BIT, NULL) != pdPASS) fail("task", 0);
}
