#!/usr/bin/env bash
set -euo pipefail
cxx=${CXX:-g++}
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -pedantic)
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -O1)
fi
"$cxx" "${flags[@]}" tests/live_overlap_quality.cpp -o /tmp/skell-live-overlap
/tmp/skell-live-overlap
