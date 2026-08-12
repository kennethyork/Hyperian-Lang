#!/bin/sh
set -eu

compiler=$1
source=$2
pack=$3
application=$4

platform=$("$compiler" platform)
if test "$platform" = linux-x64; then
    wanted=linux-arm64
else
    wanted=linux-x64
fi
if output=$("$compiler" build "$source" for "$wanted" using "$pack" as "$application" 2>&1); then
    echo "expected a runtime pack for the wrong platform to be rejected" >&2
    exit 1
fi
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -F "not $wanted" >/dev/null
test ! -e "$application"
