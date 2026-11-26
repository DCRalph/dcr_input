#!/bin/sh
# Builds and runs the host tests against the fake Arduino in test/shim.
set -e
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/dcr_input_tests"
c++ -std=gnu++17 -Wall -Wextra -Werror \
  -fsanitize=address,undefined -Itest/shim -Isrc \
  src/dcr_input/*.cpp test/test_main.cpp -o "$out"
"$out"
