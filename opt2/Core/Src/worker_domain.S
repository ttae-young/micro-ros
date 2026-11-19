/* MPU syscall veneer and exact diagnostic fault sites only. App logic is C. */
#include "benchmark_config.h"
.syntax unified
.thumb
.section .rodata.bench_policy_mode,"a",%progbits
.global bench_policy_mode
.type bench_policy_mode,%object
bench_policy_mode: .word BENCH_MODE
.size bench_policy_mode,4
#if BENCH_MPU_ENABLED
.section freertos_system_calls,"ax",%progbits
#else
.section .text.app_gate,"ax",%progbits
#endif
.align 2
.global app_gate
.type app_gate,%function
.thumb_func
app_gate:
#if BENCH_MPU_ENABLED
 svc #71
 bx lr
#else
 b.w app_gate_impl
#endif
.size app_gate,.-app_gate
.section .text.app_probe_read,"ax",%progbits
.align 2
.global app_probe_read,pub_load_site,sub_load_site
.type app_probe_read,%function
.thumb_func
app_probe_read:
 mrs r3,control
pub_load_site:
sub_load_site:
 ldr r0,[r0]
 bx lr
.size app_probe_read,.-app_probe_read
.section .text.app_probe_write,"ax",%progbits
.align 2
.global app_probe_write,pub_store_site,sub_store_site
.type app_probe_write,%function
.thumb_func
app_probe_write:
 mrs r3,control
pub_store_site:
sub_store_site:
 str r1,[r0]
 movw r0,#0x5a5a
 bx lr
.size app_probe_write,.-app_probe_write
.section .text.app_probe_recover,"ax",%progbits
.align 2
.global pub_commit_site,sub_commit_site
pub_commit_site:
sub_commit_site:
 bx lr
