#!/bin/sh
set -eu

compiler=$1
source=$2
pack=$3
stale=$4
application=$5

mkdir "$stale"
cp "$pack/runtime" "$stale/runtime"
version=$("$compiler" version | awk '{print $2}')
sed "s/toolchain: $version/toolchain: 0.1.0/" "$pack/hyperian.runtime" > "$stale/hyperian.runtime"
platform=$("$compiler" platform)
if output=$("$compiler" build "$source" for "$platform" using "$stale" as "$application" 2>&1); then
    echo "expected an outdated runtime pack to be rejected" >&2
    exit 1
fi
printf '%s\n' "$output"
printf '%s\n' "$output" | grep -F "uses Hyperian 0.1.0 but this compiler is Hyperian $version" >/dev/null
test ! -e "$application"
