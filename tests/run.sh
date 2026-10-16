#!/usr/bin/env bash
# Host checks for tiles, settings, the librespot adapter and the panel's model (any C++20 compiler).
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
cxx=${CXX:-g++}
mkdir -p "$repo/build/tests"
"$cxx" -std=c++20 -Wall -Wextra -Werror -Wno-deprecated-literal-operator \
    -I"$repo/src/common" -I"$repo/src/ui" -I"$repo/src/daemon" -I"$repo/.deps/nlohmann/include" \
    "$repo/tests/tiles.cpp" -o "$repo/build/tests/tiles"
"$repo/build/tests/tiles"
