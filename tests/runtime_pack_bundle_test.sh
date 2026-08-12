#!/bin/sh
set -eu

compiler=$1
source=$2
pack=$3
bundle=$4
outside=$5

platform=$("$compiler" platform)
version=$("$compiler" version | awk '{print $2}')
"$compiler" bundle "$source" for "$platform" using "$pack" to "$bundle"
grep -F "format: HYBN1" "$bundle/hyperian.bundle" >/dev/null
grep -F "toolchain: $version" "$bundle/hyperian.bundle" >/dev/null
grep -F "platform: $platform" "$bundle/hyperian.bundle" >/dev/null
grep -F "executable: run" "$bundle/hyperian.bundle" >/dev/null
test -x "$bundle/run"
test "$(cat "$bundle/assets/message.txt")" = "Hello from a bundled asset."
mkdir "$outside"
cd "$outside"
"$bundle/run"
