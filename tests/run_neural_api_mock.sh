#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
"${CXX:-c++}" -std=c++17 -O2 -pthread -Wall -Wextra -Wpedantic -Werror \
 -DSKELL_WITH_RNNOISE=1 -DSKELL_TEST_RNNOISE_API=1 \
 -Itests/mock_rnnoise -Itests/shim -ISource tests/neural_contract.cpp \
 tests/mock_rnnoise/rnnoise_mock.cpp Source/dsp/StreamingNeural.cpp \
 Source/dsp/Separator.cpp Source/dsp/SpectralAnalyzer.cpp \
 -o /tmp/skell-rnnoise-api-mock
/tmp/skell-rnnoise-api-mock
