#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out=$(mktemp -d /tmp/worker-tests.XXXXXX)
for n in 1 4; do
 gcc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -DBENCH_WORKER_HOST_TEST -DBENCH_OPT3=1 -DBENCH_MODE=2 -DBENCH_SUBSCRIBERS=$n -ICore/Inc -Itests tests/worker_test.c -o "$out/test"
 "$out/test"
done
