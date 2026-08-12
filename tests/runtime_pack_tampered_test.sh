#!/bin/sh
set -eu

compiler=$1
source=$2
pack=$3
tampered=$4
application=$5

mkdir "$tampered"
cp "$pack/runtime" "$tampered/runtime"
cp "$pack/hyperian.runtime" "$tampered/hyperian.runtime"
printf 'damaged' >> "$tampered/runtime"
platform=$("$compiler" platform)
if output=$("$compiler" build "$source" for "$platform" using "$tampered" as "$application" 2>&1); then
    echo "expected a changed runtime executable to be rejected" >&2
    exit 1
fi
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -F "does not match its manifest" >/dev/null
test ! -e "$application"
