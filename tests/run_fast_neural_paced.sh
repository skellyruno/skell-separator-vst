#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
compiler=${CXX:-g++}
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -pedantic -pthread)
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -O1)
fi
"$compiler" "${flags[@]}" tests/fast_neural_paced.cpp -o /tmp/skell-fast-paced
/tmp/skell-fast-paced
