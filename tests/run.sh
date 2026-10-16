#!/usr/bin/env bash
# Host checks for the interface's model and the library browser (any C++20 compiler).
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
json="$repo/.deps/nlohmann/include"
cxx=${CXX:-g++}
mkdir -p "$repo/build/tests"
for test in browse native_model; do
    "$cxx" -std=c++20 -Wall -Wextra -Werror -I"$repo/src/ui" -I"$json" \
        "$repo/tests/$test.cpp" -o "$repo/build/tests/$test"
    "$repo/build/tests/$test"
done
