#!/bin/sh
# GBC-specific mechanics have intentionally diverged from the GBA/Rust controls.
# Keep their shared fixtures untouched; exercise the shipping CGB rules directly.
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d /private/tmp/beaver-rules.XXXXXX)
for case_name in course shop surf race race_ai wave_physics; do
    cc -std=c99 -Wall -Wextra -O1 \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -o "$test_dir/$case_name" gbc/src/rules.c gbc/src/wave.c \
        gbc/src/actors.c gbc/src/save.c gbc/src/shop.c gbc/src/surf.c \
        gbc/src/race.c "gbc/test/${case_name}_test.c"
    "$test_dir/$case_name"
done
