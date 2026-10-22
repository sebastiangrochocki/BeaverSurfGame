#!/bin/sh
# Builds the C rules with the host compiler and replays the golden traces through them.
# The sanitizers flag any arithmetic that would silently truncate on a 16-bit target.
set -eu
cd "$(dirname "$0")/.."
out="${TMPDIR:-/tmp}/beaver_trace_test"
cc -std=c99 -Wall -Wextra -Wconversion -O1 \
    -fsanitize=undefined,implicit-conversion -fno-sanitize-recover=all \
    -o "$out" gbc/src/rules.c gbc/src/wave.c gbc/src/save.c gbc/test/trace_test.c
"$out" fixtures
