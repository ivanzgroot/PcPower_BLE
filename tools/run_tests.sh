#!/usr/bin/env bash
# Compiles and runs the native unit tests for PcPower_BLE/src/core.
# No hardware, no Arduino, no network required.
set -euo pipefail
cd "$(dirname "$0")/.."

mkdir -p tests/bin
CORE_SRC=$(ls PcPower_BLE/src/core/*.cpp 2>/dev/null || true)

echo "==> compiling tests"
g++ -std=c++17 -Wall -Wextra -Werror -O1 -g \
    -I PcPower_BLE/src -I tests \
    tests/*.cpp ${CORE_SRC} \
    -o tests/bin/run_tests

echo "==> running tests"
./tests/bin/run_tests
