#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror)
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then
 flags=(-std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
bin="${SKELL_TEST_BINARY:-/tmp/skell-neural-blend}"
"${CXX:-c++}" "${flags[@]}" -Itests/shim -ISource tests/neural_blend.cpp -o "$bin"
"$bin"
