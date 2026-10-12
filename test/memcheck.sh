#!/bin/sh

set -eu

VALGRIND="${VALGRIND:-valgrind}"
DEBUGINFOD="${DEBUGINFOD:-0}"
PASSLIT="${PASSLIT:-./mem/tool}"
SUPPRESSIONS="${SUPPRESSIONS:-}"
LOG="$(mktemp)"
trap 'rm -f -- "$LOG"' EXIT HUP INT TERM

case "$DEBUGINFOD" in
    0) DOWNLOAD="no" ;;
    1) DOWNLOAD="yes" ;;
    *)
        printf -- "%s\n" "DEBUGINFOD must be 0 or 1" >&2
        exit 64
        ;;
esac

check() {
    EXPECTED="$1"
    shift

    ACTUAL=0
    if [ -n "$SUPPRESSIONS" ]; then
        set -- --soname-synonyms=somalloc=NONE \
            "--suppressions=${SUPPRESSIONS}" "$PASSLIT" "$@"
    else
        set -- "$PASSLIT" "$@"
    fi
    "$VALGRIND" -s --tool=memcheck --leak-check=full \
        --log-file="$LOG" \
        --enable-debuginfod="$DOWNLOAD" \
        --show-leak-kinds=all --errors-for-leak-kinds=all \
        --track-origins=yes --error-exitcode=99 \
        "$@" >/dev/null || ACTUAL=$?
    cat -- "$LOG" >&2

    if [ "$ACTUAL" -ne "$EXPECTED" ]; then
        printf -- "Expected status %s, got %s\n" \
            "$EXPECTED" "$ACTUAL" >&2
        exit 1
    fi
    if ! grep -Eq -- 'total heap usage: [1-9][0-9,]* allocs' "$LOG"; then
        printf -- "%s\n" "Memcheck did not track allocations" >&2
        exit 1
    fi
}

check 0 -n 3
check 0 -p -n 3
check 0 --seed "memcheck" -n 3
check 0 -p --seed "memcheck" -n 3
check 0 -a "D*c*" -c "33aab" -l 12 -n 3
check 65 -a "c*" -c "xxx" -l 5
check 0 -p -a "c*" -c "xxx" -l 5
check 65 -a "U*D*" -l 1
check 65 -a "Uc*" -c "AA"
check 64 -a "c" -c "a b"

if "$PASSLIT" -n 1 -j 2 >/dev/null 2>&1; then
    check 0 --seed "memcheck" -n 257 -j 4
fi

printf -- "%s\n" "Memcheck passed."
