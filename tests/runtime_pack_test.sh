#!/bin/sh
set -eu

compiler=$1
source=$2
pack=$3
application=$4

platform=$("$compiler" platform)
version=$("$compiler" version | awk '{print $2}')
grep -F "format: HYRP1" "$pack/hyperian.runtime" >/dev/null
grep -F "toolchain: $version" "$pack/hyperian.runtime" >/dev/null
grep -F "bytecode: HYC1" "$pack/hyperian.runtime" >/dev/null
grep -F "platform: $platform" "$pack/hyperian.runtime" >/dev/null
grep -E '^checksum: [0-9a-f]{16}$' "$pack/hyperian.runtime" >/dev/null
test -x "$pack/runtime"
"$compiler" build "$source" for "$platform" using "$pack" as "$application"
"$application"
