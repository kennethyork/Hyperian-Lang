#!/bin/sh
set -eu

compiler=$1
bundle=$2
platform=$("$compiler" platform)
version=$("$compiler" version | awk '{print $2}')
grep -F "format: HYBN1" "$bundle/hyperian.bundle" >/dev/null
grep -F "toolchain: $version" "$bundle/hyperian.bundle" >/dev/null
grep -F "platform: $platform" "$bundle/hyperian.bundle" >/dev/null
grep -F "executable: run" "$bundle/hyperian.bundle" >/dev/null
