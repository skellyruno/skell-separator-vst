#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(-std=c++17 -O2 -pthread -Wall -Wextra -Wpedantic -Werror)
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then
    flags=(-std=c++17 -O1 -g -pthread -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-c++}" "${flags[@]}" tests/stemgen_transport_contract.cpp Source/dsp/StemgenRTBridge.cpp Source/dsp/StreamingNeural.cpp -o "${SKELL_TEST_BINARY:-/tmp/skell-stemgen-transport}"
"${SKELL_TEST_BINARY:-/tmp/skell-stemgen-transport}"
