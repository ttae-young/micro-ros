#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out=$(mktemp -d /tmp/lowfat-tests.XXXXXX)
common=(-DBENCH_OPT3=${OPT3:-1} -O2 -std=c11 -g -Wall -Wextra -Werror -fsanitize=address,undefined -DLOWFAT_HOST_TEST -ICore/Inc -pthread)
gcc "${common[@]}" Core/Src/lowfat_mcu.c Core/Src/lowfat_config_stm32.c \
  Core/Src/lowfat_extra_mcu.c Core/Src/lowfat_fallback_mcu.c \
  tests/lowfat_tests.c tests/host_main.c -o "$out/core"
"$out/core"
gcc "${common[@]}" -DBENCH_MODE=1 Core/Src/lowfat_mcu.c \
  Core/Src/lowfat_config_stm32.c Core/Src/microros_allocators.c \
  tests/adapter_test.c -o "$out/adapter"
"$out/adapter"
echo "Test executables: $out"

gcc "${common[@]}" -include tests/reference_names.h -c tests/lowfat_before.c -o "$out/reference.o"
gcc "${common[@]}" Core/Src/lowfat_mcu.c Core/Src/lowfat_config_stm32.c \
  tests/differential_test.c "$out/reference.o" -o "$out/differential"
"$out/differential"
for fast in 0 1; do
  gcc "${common[@]}" -DBENCH_FAST_PATHS=$fast Core/Src/lowfat_config_stm32.c tests/fast_paths_test.c -o "$out/fast$fast"
  "$out/fast$fast"
done
