#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
compiler=${CXX:-g++}
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -pedantic)
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -O1)
fi
"$compiler" "${flags[@]}" tests/measured_room_dereverb_research.cpp -o /tmp/skell-measured-dereverb
/tmp/skell-measured-dereverb
