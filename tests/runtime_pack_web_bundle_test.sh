#!/bin/sh
set -eu

compiler=$1
source=$2
pack=$3
bundle=$4

platform=$("$compiler" platform)
"$compiler" bundle "$source" for "$platform" using "$pack" to "$bundle"
test -x "$bundle/run"
test -f "$bundle/public/app.css"
test -f "$bundle/public/icon.svg"
grep -F '"display": "standalone"' "$bundle/public/manifest.webmanifest" >/dev/null
grep -F "platform: $platform" "$bundle/hyperian.bundle" >/dev/null
