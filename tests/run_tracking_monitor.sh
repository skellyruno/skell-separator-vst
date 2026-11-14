#!/usr/bin/env bash
set -euo pipefail
CXX=${CXX:-g++}
FLAGS=(-std=c++17 -Wall -Wextra -Werror -pedantic)
if [[ "${SKELL_SANITIZE:-0}" == "1" ]]; then FLAGS+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"$CXX" "${FLAGS[@]}" tests/tracking_monitor.cpp -o /tmp/skell-tracking-monitor
/tmp/skell-tracking-monitor
