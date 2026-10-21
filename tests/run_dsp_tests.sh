#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror)
bin="${SKELL_TEST_BINARY:-/tmp/skell-separator-dsp-regression}"
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then
  flags=(-std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror
         -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-c++}" "${flags[@]}" -Itests/shim -ISource tests/dsp_regression.cpp \
  Source/dsp/Separator.cpp Source/dsp/StemMixer.cpp Source/dsp/SpectralAnalyzer.cpp \
  -o "$bin"
"$bin"
