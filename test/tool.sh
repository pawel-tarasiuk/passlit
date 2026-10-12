#!/bin/sh

set -eu

PASSLIT="${PASSLIT:-../passlit}"
TOOL="$PASSLIT"

legacy() {
    "$TOOL" -p "$@"
}

PASSLIT="legacy"

[ "$("$PASSLIT" --version)" = "passlit 0.0.1" ]

[ "$("$PASSLIT" -V)" = "passlit 0.0.1" ]

OUT="$("$PASSLIT" -a "D" -l 8 -n 5)"
[ "$(printf -- "%s\n" "$OUT" | wc -l)" -eq 5 ]

if printf -- "%s\n" "$OUT" | LC_ALL="C" grep -Ev -- "^[34679]{8}\$"; then
    exit 1
fi

[ "$("$PASSLIT" -a "D" -b 7 -R "down" | wc -c)" -eq 4 ]

[ "$("$PASSLIT" -a "D" -b 7 -R "up" | wc -c)" -eq 5 ]

[ "$("$PASSLIT" | wc -c)" -eq 23 ]

[ "$("$PASSLIT" -b 128 | wc -c)" -eq 23 ]

[ "$("$PASSLIT" -b 128 -R "up" | wc -c)" -eq 24 ]

[ "$("$PASSLIT" -b 128 -R "down" | wc -c)" -eq 23 ]

expect_status() {
    EXPECTED="$1"

    shift

    ACTUAL=0
    "$PASSLIT" "$@" >/dev/null 2>&1 || ACTUAL=$?

    [ "$ACTUAL" -eq "$EXPECTED" ]
}

expect_status 64 -a "Uu"

expect_status 64 -a ""

expect_status 64 -l 8 -b 32

expect_status 0 -R "down"

expect_status 64 -l 8 -R "down"

expect_status 64 -r "U"

expect_status 64 --require "U"

expect_status 64 -n 0

expect_status 64 -l -1

expect_status 64 -n -1

expect_status 64 -b -1

expect_status 64 -l " 8"

expect_status 64 -l "8x"

expect_status 64 -l ""

expect_status 64 -l 99999999999999999999999999

expect_status 65 -l 3 -a "U*L*D*O*"

ACTUAL=0
OUT="$("$PASSLIT" -l 3 -a "U*L*D*O*" --seed "short" -n 3 \
    2>/dev/null)" || ACTUAL=$?
[ "$ACTUAL" -eq 65 ]

[ "$OUT" = "$("$PASSLIT" -l 4 --seed "short" -n 3)" ]

ACTUAL=0
OUT="$("$PASSLIT" -l 0 -a "D" --seed "zero" 2>/dev/null)" || ACTUAL=$?
[ "$ACTUAL" -eq 65 ]

[ "$OUT" = "$("$PASSLIT" -l 1 -a "D" --seed "zero")" ]

expect_status 65 -a "D" -b 0 -R "down"

expect_status 0 --help

printf -- "%s\n" "Tool checks passed."

[ "$("$PASSLIT" -a "c*" -c "xxx" -l 5)" = "xxxxx" ]

[ "$("$PASSLIT" -a "c" -c "ab" -b 8 -R "down" | wc -c)" -eq 9 ]

[ "$("$PASSLIT" -a "D*c*" -c "x" -b 4 -R "up" | wc -c)" -eq 4 ]

[ "$("$PASSLIT" -a "D*c*" -c "x" -b 4 -R "down" | wc -c)" -eq 3 ]

[ "$("$PASSLIT" -a "c" -c "x" -l 4096 | wc -c)" -eq 4097 ]

OUT="$("$PASSLIT" -a "U*c*" -c "AA!!" -l 2 -n 10)"

if printf -- "%s\n" "$OUT" | LC_ALL="C" grep -Ev -- \
    "^([ACEFHKLMNPRTVXYZ]!|![ACEFHKLMNPRTVXYZ])\$"; then
    exit 1
fi

expect_status 64 -a "c"

expect_status 64 -a "c" -c ""

# Custom pools reject every ASCII whitespace character.
for SPACE in " " "$(printf "\t")" "$(printf "\r")" \
    "$(printf "\v")" "$(printf "\f")" "$(printf "\nx")"; do
    expect_status 64 -a "c" -c "a${SPACE}b"
done

expect_status 64 -a "U" -c "abc"

expect_status 64 -a "C" -c "abc"

expect_status 64 -a "U**"

expect_status 64 -a "*U"

expect_status 64 -a "cc" -c "abc"

expect_status 65 -a "Uc*" -c "AA"

expect_status 65 -a "U*L*D*O*c*" -c "0" -l 4

expect_status 0 -a "Uc" -c "AA"

expect_status 0 -a "U*c*" -c "A!" -l 2

expect_status 65 -a "c" -c "x" -b 1

expect_status 65 -a "c" -c "x" -b 0 -R "down"

printf -- "%s\n" "Custom-pool and inline-requirement Tool checks passed."

EXPECTED="$(printf -- "%s\n" "dcadcadc" "addaadcb")"
[ "$("$PASSLIT" --seed "example" -a "c" -c "abcd" -l 8 -n 2)" = "$EXPECTED" ]

[ "$("$PASSLIT" --seed "" -a "c" -c "abcd" -l 8)" = "adcaddab" ]

printf -- "%s\n" "Seeded batch checks passed."

ACTUAL=0
OUT="$("$PASSLIT" --seed "parallel" -n 103 -j 4 2>/dev/null)" || ACTUAL=$?
if [ "$ACTUAL" -eq 0 ]; then
    [ "$OUT" = "$("$PASSLIT" --seed "parallel" -n 103 -j 1)" ]
else
    [ "$ACTUAL" -eq 64 ]
fi
expect_status 0 -j 0

[ "$("$TOOL" -P --seed "patterns" -n 3)" = \
    "$("$TOOL" --seed "patterns" -n 3)" ]

[ "$("$TOOL" -P -p --seed "example" -a "c" -c "abcd" -l 8)" = \
    "dcadcadc" ]

ACTUAL=0
OUT="$("$TOOL" -P -a "c*" -c "xxx" -l 5 2>/dev/null)" || ACTUAL=$?
[ "$ACTUAL" -eq 65 ]

[ "$OUT" = "xxxxx" ]

[ "$("$TOOL" -P -a "c" -c "x" -l 3)" = "xxx" ]

ACTUAL=0
OUT="$("$TOOL" -P --seed "parallel" -n 257 -j 4 2>/dev/null)" || ACTUAL=$?
if [ "$ACTUAL" -eq 0 ]; then
    [ "$OUT" = "$("$TOOL" -P --seed "parallel" -n 257 -j 1)" ]
else
    [ "$ACTUAL" -eq 64 ]
fi

# Rank conversion uses stdin and preserves record order.
[ "$(printf "%s\n" 0 1 255 | "$TOOL" -e -p -a c -c ab -l 8 |
    "$TOOL" -r -p -a c -c ab -l 8 -x)" = \
    "$(printf "%s\n" "0x00" "0x01" "0xff")" ]

[ "$(printf "%s\n" "0x00" "0xff" |
    "$TOOL" --encode --hex -p -a c -c ab -l 8 |
    "$TOOL" --rank --decimal -p -a c -c ab -l 8)" = \
    "$(printf "%s\n" 0 255)" ]

[ "$("$TOOL" --test -p -a c -c ab -l 8)" = \
    "$(printf "%s\n" "length: 8" "scope: 256" "entropy_bits: 8 8")" ]

[ "$("$TOOL" --test -p -a c -c abc -l 1)" = \
    "$(printf "%s\n" "length: 1" "scope: 3" "entropy_bits: 1 2")" ]

[ "$("$TOOL" --test -p -a c -c a -l 1)" = \
    "$(printf "%s\n" "length: 1" "scope: 1" "entropy_bits: 0 0")" ]

# Hex input accepts either case; output stays lowercase.
[ "$(printf "%s\n" "0XAF" "0xaF" "Af" "ff" "xAf" "XAF" |
    "$TOOL" -e -x -p -a c -c ab -l 8 |
    "$TOOL" -r -x -p -a c -c ab -l 8)" = \
    "$(printf "%s\n" "0xaf" "0xaf" "0xaf" "0xff" "0xaf" "0xaf")" ]

# Invalid records do not hide later valid records.
ACTUAL=0
OUT="$(printf "%s\n" "aaaaaaaa" "invalid" "bbbbbbbb" |
    "$TOOL" -r -p -a c -c ab -l 8 2>/dev/null)" || ACTUAL=$?
[ "$ACTUAL" -eq 65 ]

[ "$OUT" = "$(printf "%s\n" 0 -1 255)" ]

ACTUAL=0
OUT="$(printf "%s\n" 256 -1 0 |
    "$TOOL" -e -p -a c -c ab -l 8 2>/dev/null)" || ACTUAL=$?
[ "$ACTUAL" -eq 65 ]

[ "$OUT" = "$(printf "%s\n" "" "" "aaaaaaaa")" ]

expect_status 65 -t -a "U*D*" -l 1

expect_status 64 -e -r

expect_status 64 -r "aaaaaaaa"

expect_status 64 -g -x

printf "%s\n" "Rank/encode and scope checks passed."

# Bounded input drains bad records, including embedded null bytes.
ACTUAL=0
OUT="$(printf 'aaaaaaaaaaaaaaaaaaaaaaaa\naa\000aa\naaaa' |
    "$TOOL" -r -p -a c -c ab -l 4 2>/dev/null)" || ACTUAL=$?
[ "$ACTUAL" -eq 65 ]

[ "$OUT" = "$(printf "%s\n" -1 -1 0)" ]

[ "$(printf '0\r\n15' | "$TOOL" -e -p -a c -c ab -l 4)" = \
    "$(printf "%s\n" "aaaa" "bbbb")" ]

# Workers preserve conversions across CLI batch boundaries.
ACTUAL=0
OUT="$(printf "%s\n" 0 | "$TOOL" -e -p -a c -c ab -l 4 -j 4 \
    2>/dev/null)" || ACTUAL=$?
if [ "$ACTUAL" -eq 0 ]; then
    INPUT="$("$TOOL" -a c -c abcd -l 8 -n 1025 -s "conversion" -j 1)"
    SERIAL="$(printf "%s\n" "$INPUT" |
        "$TOOL" -r -x -a c -c abcd -l 8 -j 1)"
    PARALLEL="$(printf "%s\n" "$INPUT" |
        "$TOOL" -r -x -a c -c abcd -l 8 -j 4)"
    [ "$SERIAL" = "$PARALLEL" ]

    [ "$(printf "%s\n" "$PARALLEL" |
        "$TOOL" -e -x -a c -c abcd -l 8 -j 4)" = "$INPUT" ]

    ACTUAL=0
    SERIAL="$(printf "%s\n" "bad" "$INPUT" "bad" |
        "$TOOL" -r -a c -c abcd -l 8 -j 1 2>/dev/null)" || ACTUAL=$?
    [ "$ACTUAL" -eq 65 ]

    ACTUAL=0
    PARALLEL="$(printf "%s\n" "bad" "$INPUT" "bad" |
        "$TOOL" -r -a c -c abcd -l 8 -j 4 2>/dev/null)" || ACTUAL=$?
    [ "$ACTUAL" -eq 65 ]

    [ "$SERIAL" = "$PARALLEL" ]

    ACTUAL=0
    SERIAL="$(printf "%s\n" "$SERIAL" |
        "$TOOL" -e -a c -c abcd -l 8 -j 1 2>/dev/null)" || ACTUAL=$?
    [ "$ACTUAL" -eq 65 ]

    ACTUAL=0
    PARALLEL="$(printf "%s\n" "$PARALLEL" |
        "$TOOL" -e -a c -c abcd -l 8 -j 4 2>/dev/null)" || ACTUAL=$?
    [ "$ACTUAL" -eq 65 ]

    [ "$SERIAL" = "$PARALLEL" ]
else
    [ "$ACTUAL" -eq 64 ]
fi

# Encode consumes words, including all ASCII whitespace separators.
[ "$(printf " \t0\n1\r2\v3\f4 5\t\n" |
    "$TOOL" -e -p -a c -c ab -l 4)" = \
    "$(printf "%s\n" "aaaa" "aaab" "aaba" "aabb" "abaa" "abab")" ]

[ -z "$(printf " \t\r\n\v\f" | "$TOOL" -e -p -a c -c ab -l 4)" ]

# Bad words each emit a blank; draining stops at the next separator.
ACTUAL=0
OUT="$(printf "bad 16\t00000000000000000000 1\0002\v0 15" |
    "$TOOL" -e -p -a c -c ab -l 4 2>/dev/null)" || ACTUAL=$?
[ "$ACTUAL" -eq 65 ]

[ "$OUT" = "$(printf "%s\n" "" "" "" "" "aaaa" "bbbb")" ]

[ "$(printf "0x0\tXF f" | "$TOOL" -e -x -p -a c -c ab -l 4)" = \
    "$(printf "%s\n" "aaaa" "bbbb" "bbbb")" ]

# Rank uses the same token boundaries as encode.
[ "$(printf " \taaaa\naaab\raaba\vaabb\fabaa abab \t" |
    "$TOOL" -r -p -a c -c ab -l 4 2>/dev/null)" = \
    "$(printf "%s\n" 0 1 2 3 4 5)" ]

[ -z "$(printf " \t\r\n\v\f" | "$TOOL" -r -p -a c -c ab -l 4)" ]

ACTUAL=0
OUT="$(printf "bad\taaaaaaaaaaaaaaaaaaaaaa aa\000aa\vbbbb aaaa" |
    "$TOOL" -r -p -a c -c ab -l 4 2>/dev/null)" || ACTUAL=$?
[ "$ACTUAL" -eq 65 ]

[ "$OUT" = "$(printf "%s\n" -1 -1 -1 15 0)" ]
