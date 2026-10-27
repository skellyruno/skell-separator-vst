#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bin="${SKELL_RENDER_BINARY:-/tmp/skell-render-classical}"
"${CXX:-c++}" -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror \
    -Itests/shim -ISource tests/render_classical.cpp \
    Source/dsp/Separator.cpp Source/dsp/SpectralAnalyzer.cpp -o "$bin"
if [[ "$#" -eq 0 ]]; then
  echo "Offline renderer built: $bin"
  exit 0
fi
"$bin" "$@"
