#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -pthread)
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then
    flags=(-std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -pthread
        -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
bin="${SKELL_TEST_BINARY:-/tmp/skell-neural-offline}"
"${CXX:-c++}" "${flags[@]}" -ISource tests/neural_offline.cpp \
  Source/dsp/StreamingNeural.cpp -o "$bin"
"$bin"
