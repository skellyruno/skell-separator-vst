#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -pthread -ISource \
 tests/tiny_vocal_synthetic_benchmark.cpp Source/dsp/TinyVocalNet.cpp Source/dsp/StreamingNeural.cpp \
 -o /tmp/skell-tiny-synthetic-benchmark
/tmp/skell-tiny-synthetic-benchmark
