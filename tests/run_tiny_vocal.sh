#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -pthread)
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then
    flags=(-std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -pthread
        -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-c++}" "${flags[@]}" -ISource tests/tiny_vocal_contract.cpp \
  Source/dsp/TinyVocalNet.cpp Source/dsp/StreamingNeural.cpp \
  -o "${SKELL_TEST_BINARY:-/tmp/skell-tiny-model-contract}"
"${SKELL_TEST_BINARY:-/tmp/skell-tiny-model-contract}"
