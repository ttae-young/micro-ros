# opt2 — FreeRTOS PUB/SUB MPU isolation

This directory contains the validated `freertos_app_direct` CubeIDE project.
The repository folder is named **opt2**; the internal CubeIDE project/ELF name
remains **freertos_app_direct**, so the supplied build and run scripts still match.

- Adjacent 32-byte LowFat PUB/SUB allocations; separate LowFat configuration.
- Actual SUB objects registered as micro-ROS receive buffers.
- PUB directly reads one SUB buffer after every publish (round-robin across four SUBs).
- MODE 1: MPU OFF; MODE 2: MPU ON, unauthorized reads fault and are recovered by a narrowly validated benchmark handler.
- Direct reads and expected fault recovery are inside the timed iterations.
- Shared trusted middleware; not separate isolated middleware instances. SFI is not implemented.

See [Korean build/run instructions](README_KO.md) and [validation evidence](validation/VALIDATION_KO.txt).

Known display-only issue in this measured revision: the startup `[FLOW]` line still
prints the former 5-call description. The actual `[GATE COUNT]` verifies **6 entries
per iteration / 600 per 100 samples**. The firmware is preserved as measured.
