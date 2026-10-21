#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(-std=c++17 -O2 -pthread -Wall -Wextra -Wpedantic -Werror -DSKELL_WITH_RNNOISE=1 -DSKELL_NEURAL_TEST_STUB=1)
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then
  flags=(-std=c++17 -O1 -g -pthread -Wall -Wextra -Wpedantic -Werror
         -DSKELL_WITH_RNNOISE=1 -DSKELL_NEURAL_TEST_STUB=1 -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
bin="${SKELL_TEST_BINARY:-/tmp/skell-neural-contract}"
"${CXX:-c++}" "${flags[@]}" -Itests/shim -ISource \
 tests/neural_contract.cpp Source/dsp/StreamingNeural.cpp \
 Source/dsp/Separator.cpp Source/dsp/SpectralAnalyzer.cpp -o "$bin"
"$bin"
