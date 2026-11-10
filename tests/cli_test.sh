#!/bin/sh
# End-to-end tests for the parbreak CLI. Usage: tests/cli_test.sh path/to/parbreak
set -u
PB=${1:-build/parbreak}
fails=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; fails=$((fails + 1)); }
expect_eq() { # name expected actual
    if [ "$2" = "$3" ]; then pass "$1"; else fail "$1"; printf '  expected: %s\n  actual:   %s\n' "$2" "$3"; fi
}

# golden output: justified, extra spaces alternate sides
out=$(echo "the quick brown fox jumps over the lazy dog" | "$PB" -w 16)
expect_eq "justify golden" "the  quick brown
fox  jumps  over
the lazy dog" "$out"

# every non-final line of a justified paragraph is exactly the width
"$PB" -w 50 -H examples/frog-king.txt > "$tmp/j.txt"
bad=$(awk 'BEGIN{bad=0} NF==0{prev="";next} {if (prev!="" && length(prev)!=50) bad++; prev=$0} END{print bad}' "$tmp/j.txt")
expect_eq "justified widths" "0" "$bad"
paras=$(grep -c '^$' "$tmp/j.txt")
expect_eq "paragraphs preserved" "1" "$paras"

# words survive formatting (after joining hyphenated line ends)
orig=$(tr -s ' \n' '\n\n' < examples/frog-king.txt | grep -v '^$' | tr '\n' ' ')
got=$(sed -e ':a' -e '/[a-z]-$/{N;s/-\n//;ba' -e '}' "$tmp/j.txt" | tr -s ' \n' '\n\n' | grep -v '^$' | tr '\n' ' ')
expect_eq "words round-trip" "$orig" "$got"

# right alignment pads every line to the width
w=$("$PB" -w 30 -a right examples/frog-king.txt | awk 'NF && length($0)!=30' | wc -l | tr -d ' ')
expect_eq "right aligned" "0" "$w"

# ragged right never leaves double spaces or trailing spaces
w=$("$PB" -w 30 -a left examples/frog-king.txt | grep -c '  \| $')
expect_eq "ragged right spacing" "0" "$w"

# indent
out=$(echo "one two three four" | "$PB" -w 14 -a left -i 3)
expect_eq "indent" "   one two
three four" "$out"

# hyphenation listing with built-in and custom patterns
out=$(echo "hyphenation typesetting table" | "$PB" -x)
expect_eq "hyphens built-in" "hy-phen-ation
type-set-ting
ta-ble" "$out"
printf '%% tiny\n1b\n@exceptions\nab-ab-ab\n' > "$tmp/p.txt"
out=$(echo "abbbbb ababab" | "$PB" -p "$tmp/p.txt" -x)
expect_eq "hyphens custom" "ab-b-bbb
ab-abab" "$out"  # the last point is inside the right minimum of 3

# compare mode header
out=$(echo "a b c" | "$PB" -w 10 -c | head -1)
expect_eq "compare header" "first-fit  | knuth-plass" "$out"

# stats go to stderr
err=$(echo "a b c" | "$PB" -w 10 -s 2>&1 >/dev/null)
case "$err" in "knuth-plass: 1 lines"*) pass "stats";; *) fail "stats"; echo "  $err";; esac

# stdin via '-' and multiple files
out=$(echo "x y" | "$PB" -w 10 - examples/frog-king.txt | head -1)
expect_eq "stdin dash" "x y" "$out"

# errors
"$PB" --bogus >/dev/null 2>&1; expect_eq "unknown option exit" "2" "$?"
"$PB" -w 0 </dev/null >/dev/null 2>&1; expect_eq "bad width exit" "2" "$?"
"$PB" -a diagonal </dev/null >/dev/null 2>&1; expect_eq "bad align exit" "2" "$?"
"$PB" "$tmp/missing" >/dev/null 2>&1; expect_eq "missing file exit" "1" "$?"
printf 'a!b\n' > "$tmp/bad.txt"
msg=$(echo x | "$PB" -p "$tmp/bad.txt" 2>&1 >/dev/null)
expect_eq "bad pattern message" "parbreak: $tmp/bad.txt:1: bad pattern" "$msg"

echo
if [ "$fails" -eq 0 ]; then echo "cli: all tests passed"; else echo "cli: $fails failures"; exit 1; fi
