#!/bin/sh
set -eu

wanted=$1
shift

if output=$("$@" 2>&1); then
    echo "expected the command to fail" >&2
    exit 1
fi

printf '%s\n' "$output"
printf '%s\n' "$output" | grep -F "$wanted" >/dev/null
