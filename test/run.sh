#!/bin/sh

set -eu

PREFIX="${1:-.}"

for NAME in "test_passlit" "test_random" "test_seed" "test_limits" \
    "test_patterns" "test_rank"; do
    "${PREFIX}/${NAME}"
done

for PAGE in -1 0 1024 4096 8192 65536 131072 "max"; do
    "${PREFIX}/test_platform" "$PAGE"
done

"${PREFIX}/test_platform_fallback"
sh -- "tool.sh"
