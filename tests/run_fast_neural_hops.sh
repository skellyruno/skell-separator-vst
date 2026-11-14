#!/usr/bin/env bash
set -euo pipefail
cxx=${CXX:-g++}
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -pedantic -pthread)
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -O1)
fi
"$cxx" "${flags[@]}" tests/fast_neural_hops.cpp -o /tmp/skell-fast-hops
/tmp/skell-fast-hops
