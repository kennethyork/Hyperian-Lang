#!/bin/sh
set -eu

compiler=$1
first=$2
second=$3
work=$(mktemp -d "${TMPDIR:-/tmp}/hyperian-concurrent.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$compiler" check "$first" >"$work/first.txt" 2>&1 &
first_process=$!
"$compiler" check "$second" >"$work/second.txt" 2>&1 &
second_process=$!
wait "$first_process"
wait "$second_process"

grep 'No mistakes found.' "$work/first.txt"
grep 'No mistakes found.' "$work/second.txt"
first_path=$(sed -n 's/^Compiled .* -> \(.*\) ([0-9][0-9]* instructions)\r*$/\1/p' "$work/first.txt")
second_path=$(sed -n 's/^Compiled .* -> \(.*\) ([0-9][0-9]* instructions)\r*$/\1/p' "$work/second.txt")
test -n "$first_path"
test -n "$second_path"
