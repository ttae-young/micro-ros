#ifndef BENCHMARK_CONFIG_H
#define BENCHMARK_CONFIG_H

/* Three-mode comparison. LowFat allocation hooks are identical in all modes.
 * 1: baseline; 2: MPU; 3: software-checked C apps with post-link validation.
 * Change BENCH_MODE, then Clean / Build. Do not use old worker measurements.
 */
#ifndef BENCH_MODE
#define BENCH_MODE 3
#endif
#if BENCH_MODE != 1 && BENCH_MODE != 2 && BENCH_MODE != 3
#error "Supported modes: 1 baseline, 2 MPU, 3 closed-C-app SFI"
#endif
/* Fixed combination: frame copy (1) + authenticated lookup (4).
 * SVC O3 and conditional MAIR writes are deliberately absent. */
#define BENCH_TUNE_STAGE 14
/* MPU-only experiments: 1=frame patch, 2=fixed MAIR, 4=two dynamic regions, 8=TCB reuse. */
#ifndef BENCH_TRIM_MASK
#define BENCH_TRIM_MASK 15
#endif
#if BENCH_TRIM_MASK != 15
#error "trim2 requires the validated fixed MAIR/two-region configuration"
#endif
/* 0: previous -O2 implementation; 1: equivalent fast paths. */
#ifndef BENCH_FAST_PATHS
#define BENCH_FAST_PATHS 1
#endif
#if (BENCH_FAST_PATHS != 0) && (BENCH_FAST_PATHS != 1)
#error "BENCH_FAST_PATHS must be 0 or 1"
#endif
/* 0: corrected pre-instrumentation opt2; 1: fast3 improvements.
 * All workload, LowFat checks, worker privileges and notification counts stay. */
#ifndef BENCH_OPT3
#define BENCH_OPT3 0
#endif
#if BENCH_OPT3 != 0
#error "BENCH_OPT3 must be 0 in the authenticated application gate revision"
#endif
#ifndef BENCH_ICACHE_WAYS
#define BENCH_ICACHE_WAYS 2
#endif
#if BENCH_ICACHE_WAYS != 1 && BENCH_ICACHE_WAYS != 2
#error "BENCH_ICACHE_WAYS must be 1 or 2"
#endif
#define BENCH_SFI_ENABLED (BENCH_MODE == 3)
#define BENCH_MPU_ENABLED (BENCH_MODE == 2)
#define BENCH_LOWFAT_ENABLED (BENCH_MODE != 0)
/* Objects live for the whole run. 0 validates them once before worker use;
 * 1 repeats the same checks at every service/worker boundary for comparison.
 * Neither setting changes hardware MPU permissions. No synthetic allocation
 * is included in the communication benchmark in this project. */
#ifndef BENCH_VALIDATE_EACH_MESSAGE
#define BENCH_VALIDATE_EACH_MESSAGE 0
#endif
#if BENCH_VALIDATE_EACH_MESSAGE != 0
#error "BENCH_VALIDATE_EACH_MESSAGE must be 0 in this revision"
#endif
/* Application data bytes, excluding ROS sequence metadata and transport framing. */
#ifndef BENCH_PAYLOAD_BYTES
#define BENCH_PAYLOAD_BYTES 64
#endif
#if BENCH_PAYLOAD_BYTES != 4 && BENCH_PAYLOAD_BYTES != 64 && BENCH_PAYLOAD_BYTES != 80 && BENCH_PAYLOAD_BYTES != 96
#error "Choose 4, 64, 80 or 96 payload bytes"
#endif
#define BENCH_PAYLOAD_WORDS (BENCH_PAYLOAD_BYTES / 4U)
#ifndef BENCH_CHECK_ALL_WORDS
#define BENCH_CHECK_ALL_WORDS 1
#endif
#if BENCH_CHECK_ALL_WORDS != 0 && BENCH_CHECK_ALL_WORDS != 1
#error "Choose first-word (0) or full payload verification (1)"
#endif
#define BENCH_OBJECT_BYTES 128U
#define BENCH_ALLOC_BYTES 48U
/* One publisher node + one node owning N independent subscriptions.
 * Bundled RMW: max subscriptions=5, shared receive history=4.
 * Keep fan-out <= 4 to avoid requiring a rebuilt static library. */
#ifndef BENCH_SUBSCRIBERS
#define BENCH_SUBSCRIBERS 4
#endif
#if BENCH_SUBSCRIBERS < 1 || BENCH_SUBSCRIBERS > 4
#error "BENCH_SUBSCRIBERS must be 1..4 with this bundled library"
#endif
/* 1: direct foreign read each iteration; 0: normal communication only.
 * PRE/POST isolation and full policy tests run in both workloads. */
#ifndef BENCH_DIRECT_READ
#define BENCH_DIRECT_READ 1
#endif
#if BENCH_DIRECT_READ != 0 && BENCH_DIRECT_READ != 1
#error "BENCH_DIRECT_READ must be 0 or 1"
#endif
#define BENCH_WARMUP 10U
#define BENCH_SAMPLES 100U
#define BENCH_PAUSE_MS 100U
#define BENCH_RX_TIMEOUT_MS 2000U
#define BENCH_DISCOVERY_MS 2000U
#if (BENCH_SAMPLES == 0) || (BENCH_RX_TIMEOUT_MS > 5000)
#error "Use nonzero sample count and receive timeout <= 5000 ms"
#endif
#endif






















