#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bin="${SKELL_CPU_BINARY:-/tmp/skell-dsp-cpu-profile}"
"${CXX:-c++}" -std=c++17 -O3 -march=native -DNDEBUG -Wall -Wextra -Wpedantic \
  -Itests/shim -ISource tests/cpu_profile.cpp \
  Source/dsp/Separator.cpp Source/dsp/SpectralAnalyzer.cpp -o "$bin"
"$bin" "${1:-48000}" "${2:-256}" "${3:-12}"
