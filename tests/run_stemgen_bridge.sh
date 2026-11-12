#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -pthread)
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then
    flags=(-std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -pthread -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-c++}" "${flags[@]}" tests/stemgen_bridge_contract.cpp Source/dsp/StemgenRTBridge.cpp -o "${SKELL_TEST_BINARY:-/tmp/skell-stemgen-bridge}"
"${SKELL_TEST_BINARY:-/tmp/skell-stemgen-bridge}"
